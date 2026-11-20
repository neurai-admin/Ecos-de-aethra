#include "LisVoxelAdapter.h"

#include "LisSimulation.h"

int32 FLisVoxelAdapter::Flush(FLisSimulation& Sim)
{
    if (!Classify)
    {
        UE_LOG(LogAethraLIS, Warning, TEXT("FLisVoxelAdapter::Flush called without a block classifier; %d changes dropped."), Pending.Num());
        Ignored += Pending.Num();
        Pending.Reset();
        return 0;
    }

    struct FGroup
    {
        ELisPlace Place;
        ELisEvent Type;
        ELisVoxelResource Resource;
        int32 Actor;
        int32 Blocks;
        FIntVector Cell;  // the group's change closest to the place anchor decides who can see it
        float Distance;
    };
    TArray<FGroup> Groups;

    for (const FLisVoxelChange& Change : Pending)
    {
        int32 Nearest = INDEX_NONE;
        float NearestDistance = PlaceRadiusCells;
        for (int32 P = 0; P < LisNum<ELisPlace>(); ++P)
        {
            const FIntVector Offset = Sim.World.Places[P].Cell - Change.Cell;
            const float Distance = FVector(Offset.X, Offset.Y, 0.f).Size();
            if (Distance <= NearestDistance)
            {
                NearestDistance = Distance;
                Nearest = P;
            }
        }
        const ELisVoxelResource Before = Classify(Change.Before);
        const ELisVoxelResource After = Classify(Change.After);
        // Air (id 0) and water are both "empty" space a block can be taken into or put in.
        const bool bBeforeEmpty = Change.Before == 0 || Before == ELisVoxelResource::Water;
        const bool bAfterEmpty = Change.After == 0 || After == ELisVoxelResource::Water;
        const bool bRemoved = !bBeforeEmpty && Before != ELisVoxelResource::None && bAfterEmpty;
        const bool bPlaced = bBeforeEmpty && !bAfterEmpty;
        if (Nearest == INDEX_NONE || (!bRemoved && !bPlaced))
        {
            ++Ignored;
            continue;
        }
        const ELisPlace Place = static_cast<ELisPlace>(Nearest);
        const ELisEvent Type = bRemoved ? ELisEvent::ResourceRemoved : ELisEvent::BlockPlaced;
        const ELisVoxelResource Resource = bRemoved ? Before : After;
        FGroup* Group = Groups.FindByPredicate([&](const FGroup& G)
        {
            return G.Place == Place && G.Type == Type && G.Resource == Resource && G.Actor == Change.Actor;
        });
        if (!Group)
        {
            Group = &Groups.Add_GetRef({ Place, Type, Resource, Change.Actor, 0, Change.Cell, NearestDistance });
        }
        else if (NearestDistance < Group->Distance)
        {
            Group->Cell = Change.Cell;
            Group->Distance = NearestDistance;
        }
        ++Group->Blocks;
    }
    Pending.Reset();

    for (const FGroup& G : Groups)
    {
        const float Units = G.Blocks / BlocksPerUnit;
        FLisPlaceState& State = Sim.World.Place(G.Place);
        const bool bStockResource = (G.Resource == ELisVoxelResource::Wood && G.Place == ELisPlace::Forest)
            || (G.Resource == ELisVoxelResource::Crop && G.Place == ELisPlace::Fields);
        // NPC edits come from LIS actions whose outcome already changed the abstract stock.
        if (G.Type == ELisEvent::ResourceRemoved && bStockResource && G.Actor <= LisNobody)
        {
            State.Stock = FMath::Max(0.f, State.Stock - Units);
        }
        Sim.EmitAtCell(G.Type, G.Place, G.Actor, bStockResource ? Units : 0.f, G.Cell);
    }
    return Groups.Num();
}
