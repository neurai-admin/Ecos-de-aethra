// Versioned binary persistence for the LIS. Independent of the voxel world save.
#include "LisSimulation.h"

#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
// Enums index fixed arrays after loading: an out-of-range value marks the archive as bad.
template <typename E>
void SerEnumN(FArchive& Ar, E& Value, int32 Num)
{
    uint8 Raw = static_cast<uint8>(Value);
    Ar << Raw;
    if (Ar.IsLoading() && Raw >= Num)
    {
        Ar.SetError();
        return;
    }
    Value = static_cast<E>(Raw);
}

template <typename E>
void SerEnum(FArchive& Ar, E& Value)
{
    SerEnumN(Ar, Value, LisNum<E>());
}

constexpr int32 NumSources = 2; // ELisSource
constexpr int32 NumTiers = 3;   // ELisTier

template <typename T, int32 N>
void SerFloats(FArchive& Ar, T (&Values)[N])
{
    for (T& Value : Values)
    {
        Ar << Value;
    }
}

void SerRelation(FArchive& Ar, FLisRelation& R)
{
    Ar << R.Trust << R.Respect << R.Affection << R.Fear << R.Resentment;
}

void SerEvent(FArchive& Ar, FLisEventRecord& E)
{
    Ar << E.Id;
    SerEnum(Ar, E.Type);
    SerEnum(Ar, E.Place);
    Ar << E.Hour << E.Actor << E.Target << E.Amount << E.Cell;
}

void SerMemory(FArchive& Ar, FLisMemory& M)
{
    Ar << M.EventId;
    SerEnum(Ar, M.Type);
    SerEnum(Ar, M.Place);
    Ar << M.Hour << M.LearnedHour << M.Actor << M.Target << M.Amount;
    SerEnumN(Ar, M.Source, NumSources);
    Ar << M.ToldBy << M.Confidence << M.Relevance << M.Valence;
}

void SerNpc(FArchive& Ar, FLisNpc& Npc, int32 Version)
{
    Ar << Npc.Identity.Id << Npc.Identity.Name << Npc.Identity.Age;
    SerEnum(Ar, Npc.Identity.Profession);
    Ar << Npc.Identity.Origin;
    SerFloats(Ar, Npc.Personality.Value);
    SerFloats(Ar, Npc.Needs);
    SerEnum(Ar, Npc.Goal);
    SerEnum(Ar, Npc.Place);
    Ar << Npc.Food << Npc.Wood << Npc.Coins;
    SerFloats(Ar, Npc.Skill);
    SerFloats(Ar, Npc.Expectation);
    for (FLisPlaceBelief& B : Npc.Beliefs)
    {
        Ar << B.bKnown << B.Stock << B.Risk << B.Confidence << B.Hour;
        SerEnumN(Ar, B.Source, NumSources);
    }
    Ar << Npc.BelievedFoodPrice;

    int32 MemoryCount = Npc.Memories.Num();
    Ar << MemoryCount;
    if (Ar.IsLoading())
    {
        if (MemoryCount < 0 || MemoryCount > 1 << 16)
        {
            Ar.SetError();
            return;
        }
        Npc.Memories.SetNum(MemoryCount);
    }
    for (FLisMemory& M : Npc.Memories)
    {
        SerMemory(Ar, M);
    }

    int32 RelationCount = Npc.Relations.Num();
    Ar << RelationCount;
    if (Ar.IsLoading())
    {
        if (RelationCount < 0 || RelationCount > 1 << 16)
        {
            Ar.SetError();
            return;
        }
        Npc.Relations.Reset();
        for (int32 I = 0; I < RelationCount; ++I)
        {
            int32 Other = 0;
            FLisRelation R;
            Ar << Other;
            SerRelation(Ar, R);
            Npc.Relations.Add(Other, R);
        }
    }
    else
    {
        for (TPair<int32, FLisRelation>& Pair : Npc.Relations)
        {
            Ar << Pair.Key;
            SerRelation(Ar, Pair.Value);
        }
    }

    SerEnum(Ar, Npc.Current);
    SerEnum(Ar, Npc.Intent);
    SerEnum(Ar, Npc.IntentPlace);
    Ar << Npc.IntentTarget << Npc.BusyUntil << Npc.NextThink;
    SerEnumN(Ar, Npc.Tier, NumTiers);
    Ar << Npc.Decisions << Npc.Replans << Npc.GoalChanges;
    SerFloats(Ar, Npc.ActionCount);

    // Version 2: events forgotten while they could still be retold.
    if (Version >= 2)
    {
        int32 ForgottenCount = Npc.Forgotten.Num();
        Ar << ForgottenCount;
        if (Ar.IsLoading())
        {
            if (ForgottenCount < 0 || ForgottenCount > 1 << 16)
            {
                Ar.SetError();
                return;
            }
            Npc.Forgotten.SetNum(ForgottenCount);
        }
        for (FLisForgotten& F : Npc.Forgotten)
        {
            Ar << F.EventId << F.EventHour;
        }
    }
}
}

void FLisSimulation::Serialize(FArchive& Ar)
{
    uint32 Magic = SaveMagic;
    int32 Version = SaveVersion;
    Ar << Magic << Version;
    if (Ar.IsLoading() && (Magic != SaveMagic || Version < 1 || Version > SaveVersion))
    {
        Ar.SetError();
        return;
    }

    Ar << Seed << Hour << NextEventId;
    Ar << Config.MaxDecisionsPerTick << Config.MemoryCapacity << Config.EventLogCapacity << Config.MidThinkHours << Config.FarStepHours
       << Config.NearRadius << Config.MidRadius << Config.PerceptionRadiusCells;
    SerEnum(Ar, Config.Focus);
    // These are divisors, capacities and budgets: zero or negative values would crash the next Tick.
    if (Ar.IsLoading() && (Config.MaxDecisionsPerTick < 0 || Config.MemoryCapacity < 1 || Config.EventLogCapacity < 1
        || Config.MidThinkHours < 1 || Config.FarStepHours < 1))
    {
        Ar.SetError();
        return;
    }

    for (FLisPlaceState& P : World.Places)
    {
        Ar << P.Stock << P.Capacity << P.Regen << P.Danger << P.BaseDanger << P.Pos.X << P.Pos.Y << P.Cell;
    }
    Ar << World.Granary << World.FoodPrice << World.WoodPrice << World.TradeRisk;

    Ar << Stats.Ticks << Stats.Decisions << Stats.Replans << Stats.Deferred << Stats.AggregateSteps << Stats.GoalChanges
       << Stats.Events << Stats.MemoriesFormed << Stats.MemoriesForgotten;

    int32 EventCount = Events.Num();
    Ar << EventCount;
    if (Ar.IsLoading())
    {
        if (EventCount < 0 || EventCount > 1 << 20)
        {
            Ar.SetError();
            return;
        }
        Events.SetNum(EventCount);
    }
    for (FLisEventRecord& E : Events)
    {
        SerEvent(Ar, E);
        if (Ar.IsError())
        {
            return;
        }
    }

    int32 NpcCount = Npcs.Num();
    Ar << NpcCount;
    if (Ar.IsLoading())
    {
        if (NpcCount < 0 || NpcCount > 1 << 20)
        {
            Ar.SetError();
            return;
        }
        Npcs.SetNum(NpcCount);
    }
    for (int32 I = 0; I < Npcs.Num(); ++I)
    {
        SerNpc(Ar, Npcs[I], Version);
        // Find() maps id N to index N-1.
        if (Ar.IsLoading() && Npcs[I].Identity.Id != I + 1)
        {
            Ar.SetError();
        }
        if (Ar.IsError())
        {
            return;
        }
    }
}

TArray<uint8> FLisSimulation::SaveToBytes() const
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    // Serialize only reads members while saving.
    const_cast<FLisSimulation*>(this)->Serialize(Writer);
    return Bytes;
}

bool FLisSimulation::LoadFromBytes(const TArray<uint8>& Bytes)
{
    FLisSimulation Loaded;
    FMemoryReader Reader(Bytes);
    Loaded.Serialize(Reader);
    if (Reader.IsError() || !Reader.AtEnd())
    {
        UE_LOG(LogAethraLIS, Warning, TEXT("LIS save rejected (%d bytes): wrong format or version."), Bytes.Num());
        return false;
    }
    *this = MoveTemp(Loaded);
    return true;
}

bool FLisSimulation::SaveToFile(const FString& Path) const
{
    // Write then rename, so a crash mid-write never leaves a truncated save behind.
    const FString Temp = Path + TEXT(".tmp");
    return FFileHelper::SaveArrayToFile(SaveToBytes(), *Temp) && IFileManager::Get().Move(*Path, *Temp, true);
}

bool FLisSimulation::LoadFromFile(const FString& Path)
{
    TArray<uint8> Bytes;
    return FFileHelper::LoadFileToArray(Bytes, *Path) && LoadFromBytes(Bytes);
}

uint32 FLisSimulation::Checksum() const
{
    const TArray<uint8> Bytes = SaveToBytes();
    return FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
}
