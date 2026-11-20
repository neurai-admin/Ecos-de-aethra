#include "LisElden.h"

namespace
{
// Block ids of EAethraBlock in the voxel runtime (main): Air 0, Grass 1, Dirt 2, Stone 3, Sand 4, Wood 5, Leaves 6.
ELisVoxelResource ClassifyAethraBlock(uint8 Block)
{
    switch (Block)
    {
    case 1:
    case 2:
    case 4: return ELisVoxelResource::Soil;
    case 3: return ELisVoxelResource::Stone;
    case 5: return ELisVoxelResource::Wood;
    case 6: return ELisVoxelResource::Foliage;
    default: return ELisVoxelResource::None;
    }
}

void SnapshotCounts(const FLisSimulation& Sim, TArray<int32>& Out)
{
    Out.Reset();
    for (const FLisNpc& Npc : Sim.Npcs)
    {
        Out.Append(Npc.ActionCount, LisNum<ELisAction>());
    }
}

FString TraitSummary(const FLisNpc& Npc)
{
    TArray<FString> Parts;
    for (int32 T = 0; T < LisNum<ELisTrait>(); ++T)
    {
        const float V = Npc.Personality.Value[T];
        if (V >= .65f)
        {
            Parts.Add(FString::Printf(TEXT("+%s"), LisName(static_cast<ELisTrait>(T))));
        }
        else if (V <= .35f)
        {
            Parts.Add(FString::Printf(TEXT("-%s"), LisName(static_cast<ELisTrait>(T))));
        }
    }
    return Parts.Num() ? FString::Join(Parts, TEXT(" ")) : TEXT("equilibrado");
}
}

FLisElden::FLisElden(uint32 Seed)
    : Sim(Seed)
{
    const ELisProfession Roster[] = {
        ELisProfession::Farmer, ELisProfession::Farmer, ELisProfession::Farmer, ELisProfession::Farmer, ELisProfession::Farmer,
        ELisProfession::Farmer, ELisProfession::Farmer, ELisProfession::Fisher, ELisProfession::Fisher, ELisProfession::Fisher,
        ELisProfession::Woodcutter, ELisProfession::Woodcutter, ELisProfession::Woodcutter, ELisProfession::Merchant, ELisProfession::Merchant,
        ELisProfession::Guard, ELisProfession::Guard, ELisProfession::Guard, ELisProfession::Explorer, ELisProfession::Explorer };
    for (ELisProfession Profession : Roster)
    {
        Sim.AddNpc(Profession, ELisPlace::Village);
    }

    // Starting social fabric: a few households and seeded acquaintances, each direction drawn apart.
    const int32 Count = Sim.Npcs.Num();
    for (int32 A = 0; A < Count; ++A)
    {
        for (int32 B = 0; B < Count; ++B)
        {
            if (A == B)
            {
                continue;
            }
            const bool bFamily = A / 2 == B / 2 && A % 4 < 2 && A < 16;
            const float Roll = Sim.Rand(A + 1, 1000 + B);
            if (!bFamily && Roll > .3f)
            {
                continue;
            }
            FLisRelation& R = Sim.Npcs[A].Relation(B + 1);
            const float Spread = Sim.Rand(A + 1, 2000 + B) - .5f;
            R.Affection = bFamily ? .8f + .1f * Spread : .5f + .4f * Spread;
            R.Trust = bFamily ? .75f + .1f * Spread : .5f + .3f * (Sim.Rand(A + 1, 3000 + B) - .5f);
            R.Respect = .5f + .2f * (Sim.Rand(A + 1, 4000 + B) - .5f);
        }
    }

    Adapter.Classify = &ClassifyAethraBlock;
}

void FLisElden::Run(int32 Hours)
{
    for (int32 H = 0; H < Hours; ++H)
    {
        Sim.Tick(1);
        Perturb();
    }
}

void FLisElden::Perturb()
{
    const int64 Now = Sim.Hour;
    FLisWorld& World = Sim.World;
    if (Now == ShortageHour)
    {
        // Blight on the fields and spoiled stores: a fact of the world, announced in the village square.
        World.Place(ELisPlace::Fields).Stock *= .1f;
        World.Granary *= .25f;
        Sim.Emit(ELisEvent::FoodShortage, ELisPlace::Village, LisNobody, LisNobody, World.Granary);
        ShortageChoices = DescribeChoices();
        SnapshotCounts(Sim, CrisisStartCounts);
    }
    else if (Now == ShortageHour + CrisisHours)
    {
        SnapshotCounts(Sim, CrisisEndCounts);
    }
    else if (Now == BerriesHour)
    {
        World.Place(ELisPlace::Wilds).Stock = 50.f; // nobody is told; it has to be found
    }
    else if (Now == MarketShockHour)
    {
        World.TradeRisk = .45f;
    }
    else if (Now == MarketShockHour + 72)
    {
        World.TradeRisk = .12f;
    }
    else if (Now == WolvesHour)
    {
        World.Place(ELisPlace::Forest).Danger = .2f;
        World.Place(ELisPlace::Wilds).Danger = .25f;
    }
    else if (Now >= LoggingHour && Now < LoggingHour + 4)
    {
        // A player clears trees near the forest anchor (96, 48) over four hours: 64 wood and 32 leaf blocks.
        const int32 Batch = static_cast<int32>(Now - LoggingHour);
        for (int32 X = 2 * Batch; X < 2 * Batch + 2; ++X)
        {
            for (int32 Y = 0; Y < 8; ++Y)
            {
                Adapter.OnBlockChanged({ FIntVector(100 + X, 50 + Y, 40), 5, 0, LisPlayer });
                if (Y < 4)
                {
                    Adapter.OnBlockChanged({ FIntVector(100 + X, 50 + Y, 46), 6, 0, LisPlayer });
                }
            }
        }
        Adapter.Flush(Sim);
    }
}

TArray<int32> FLisElden::CrisisActions(int32 NpcIndex) const
{
    TArray<int32> Delta;
    const int32 Stride = LisNum<ELisAction>();
    Delta.SetNumZeroed(Stride);
    if (CrisisStartCounts.Num() == Sim.Npcs.Num() * Stride && CrisisEndCounts.Num() == CrisisStartCounts.Num())
    {
        for (int32 A = 0; A < Stride; ++A)
        {
            Delta[A] = CrisisEndCounts[NpcIndex * Stride + A] - CrisisStartCounts[NpcIndex * Stride + A];
        }
    }
    return Delta;
}

FString FLisElden::DescribeChoices(int32 TopN) const
{
    FString Out;
    for (const FLisNpc& Npc : Sim.Npcs)
    {
        TArray<FLisOption> Options = Sim.Evaluate(Npc);
        Options.Sort([](const FLisOption& A, const FLisOption& B) { return A.Score > B.Score; });
        Out += FString::Printf(TEXT("| %s | %s | %s | fome %.2f, comida %.1f, moedas %.1f |"),
            *Npc.Identity.Name, LisName(Npc.Identity.Profession), *TraitSummary(Npc), Npc.Need(ELisNeed::Food), Npc.Food, Npc.Coins);
        for (int32 I = 0; I < FMath::Min(TopN, Options.Num()); ++I)
        {
            Out += FString::Printf(TEXT(" %s %.2f |"), LisName(Options[I].Action), Options[I].Score);
        }
        Out += TEXT("\n");
    }
    return Out;
}

FString FLisElden::Report() const
{
    const FLisStats& S = Sim.Stats;
    FString Out = FString::Printf(TEXT("# Vila de Elden: relatório gerado\n\nSeed %u, %lld horas simuladas (%lld dias), %d NPCs.\n\n"),
        Sim.Seed, Sim.Hour, Sim.Hour / 24, Sim.Npcs.Num());

    Out += TEXT("## Desempenho\n\n| Métrica | Valor |\n| --- | --- |\n");
    Out += FString::Printf(TEXT("| Tempo médio por hora simulada | %.4f ms |\n| Pior hora simulada | %.4f ms |\n"), S.TotalTickMs / FMath::Max<int64>(1, S.Ticks), S.MaxTickMs);
    Out += FString::Printf(TEXT("| Decisões | %lld (%.2f por NPC por dia) |\n"), S.Decisions, S.Decisions / FMath::Max(1.0, Sim.Npcs.Num() * Sim.Hour / 24.0));
    Out += FString::Printf(TEXT("| Replanejamentos | %lld (%.1f%% das decisões) |\n"), S.Replans, 100.0 * S.Replans / FMath::Max<int64>(1, S.Decisions));
    Out += FString::Printf(TEXT("| Decisões adiadas pelo orçamento | %lld |\n| Mudanças de objetivo | %lld |\n| Eventos | %lld |\n"), S.Deferred, S.GoalChanges, S.Events);
    Out += FString::Printf(TEXT("| Memórias atuais / formadas / esquecidas | %d / %lld / %lld |\n"), Sim.MemoryCount(), S.MemoriesFormed, S.MemoriesForgotten);
    Out += FString::Printf(TEXT("| Memória aproximada da simulação | %.1f KB |\n| Save | %d bytes |\n\n"), Sim.ApproxBytes() / 1024.0, Sim.SaveToBytes().Num());

    Out += TEXT("## Habitantes\n\n| NPC | Profissão | Traços | Objetivo final (mudanças) | Ações mais frequentes | Memórias vistas/ouvidas | Habilidade | Maior afeto | Maior ressentimento |\n| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n");
    for (const FLisNpc& Npc : Sim.Npcs)
    {
        TArray<int32> Order;
        for (int32 A = 0; A < LisNum<ELisAction>(); ++A)
        {
            if (A != LisIdx(ELisAction::Travel) && Npc.ActionCount[A] > 0)
            {
                Order.Add(A);
            }
        }
        Order.Sort([&Npc](int32 A, int32 B) { return Npc.ActionCount[A] > Npc.ActionCount[B]; });
        FString Actions;
        for (int32 I = 0; I < FMath::Min(3, Order.Num()); ++I)
        {
            Actions += FString::Printf(TEXT("%s%s %d"), I ? TEXT(", ") : TEXT(""), LisName(static_cast<ELisAction>(Order[I])), Npc.ActionCount[Order[I]]);
        }
        int32 Seen = 0;
        for (const FLisMemory& M : Npc.Memories)
        {
            Seen += M.Source == ELisSource::Observed;
        }
        FString Fond = TEXT("-"), Grudge = TEXT("-");
        float BestAffection = .6f, BestResentment = .1f;
        for (const TPair<int32, FLisRelation>& Pair : Npc.Relations)
        {
            const FString Who = Pair.Key == LisPlayer ? FString(TEXT("jogador")) : Sim.Find(Pair.Key) ? Sim.Find(Pair.Key)->Identity.Name : FString(TEXT("?"));
            if (Pair.Value.Affection > BestAffection)
            {
                BestAffection = Pair.Value.Affection;
                Fond = FString::Printf(TEXT("%s %.2f"), *Who, Pair.Value.Affection);
            }
            if (Pair.Value.Resentment > BestResentment)
            {
                BestResentment = Pair.Value.Resentment;
                Grudge = FString::Printf(TEXT("%s %.2f"), *Who, Pair.Value.Resentment);
            }
        }
        Out += FString::Printf(TEXT("| %s | %s | %s | %s (%d) | %s | %d/%d | %.2f | %s | %s |\n"),
            *Npc.Identity.Name, LisName(Npc.Identity.Profession), *TraitSummary(Npc), LisName(Npc.Goal), Npc.GoalChanges, *Actions,
            Seen, Npc.Memories.Num() - Seen, Npc.Skill[LisIdx(Npc.Identity.Profession)], *Fond, *Grudge);
    }

    Out += FString::Printf(TEXT("\n## Escassez (hora %lld): três melhores opções de cada NPC\n\n| NPC | Profissão | Traços | Estado | 1ª | 2ª | 3ª |\n| --- | --- | --- | --- | --- | --- | --- |\n%s"), ShortageHour, *ShortageChoices);
    Out += FString::Printf(TEXT("\n## O que cada NPC iniciou nas %lld horas seguintes à escassez\n\n"), CrisisHours);
    for (int32 I = 0; I < Sim.Npcs.Num(); ++I)
    {
        const TArray<int32> Delta = CrisisActions(I);
        FString Line;
        for (int32 A = 0; A < Delta.Num(); ++A)
        {
            if (Delta[A] > 0 && A != LisIdx(ELisAction::Travel))
            {
                Line += FString::Printf(TEXT("%s %d; "), LisName(static_cast<ELisAction>(A)), Delta[A]);
            }
        }
        Out += FString::Printf(TEXT("- %s (%s): %s\n"), *Sim.Npcs[I].Identity.Name, LisName(Sim.Npcs[I].Identity.Profession), *Line);
    }

    Out += TEXT("\n## Quem sabe das frutas no Ermo\n\n");
    for (const FLisNpc& Npc : Sim.Npcs)
    {
        const FLisMemory* Found = Npc.Memories.FindByPredicate([](const FLisMemory& M) { return M.Type == ELisEvent::ResourceFound; });
        if (Found)
        {
            const FLisNpc* Teller = Sim.Find(Found->ToldBy);
            Out += FString::Printf(TEXT("- %s: %s, confiança %.2f\n"), *Npc.Identity.Name,
                Found->Source == ELisSource::Observed ? TEXT("viu") : *FString::Printf(TEXT("ouviu de %s"), Teller ? *Teller->Identity.Name : TEXT("?")), Found->Confidence);
        }
    }

    Out += TEXT("\n## Corte de árvores pelo jogador (adaptador voxel)\n\n");
    bool bAnyWitness = false;
    for (const FLisNpc& Npc : Sim.Npcs)
    {
        for (const FLisMemory& M : Npc.Memories)
        {
            if (M.Type == ELisEvent::ResourceRemoved)
            {
                bAnyWitness = true;
                Out += FString::Printf(TEXT("- %s (%s): %s, valência %.2f, relevância %.2f\n"), *Npc.Identity.Name, LisName(Npc.Identity.Profession),
                    M.Source == ELisSource::Observed ? TEXT("viu") : TEXT("ouviu"), M.Valence, M.Relevance);
                break;
            }
        }
    }
    if (!bAnyWitness)
    {
        Out += TEXT("Ninguém estava na floresta durante o corte, e ninguém lembra dele.\n");
    }

    Out += TEXT("\n## Mudanças de objetivo registradas\n\n");
    for (const FLisEventRecord& E : Sim.Events)
    {
        if (E.Type == ELisEvent::GoalChanged)
        {
            Out += FString::Printf(TEXT("- Dia %lld: %s passou a %s\n"), E.Hour / 24, *Sim.Find(E.Actor)->Identity.Name, LisName(static_cast<ELisGoal>(FMath::RoundToInt(E.Amount))));
        }
    }
    return Out;
}
