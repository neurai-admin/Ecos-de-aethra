#include "AethraLisVillage.h"

#include "AethraLisNav.h"
#include "AethraVoxelWorld.h"
#include "Voxel/AethraTerrain.h"

namespace
{
using FGenColumn = TStaticArray<EAethraBlock, AethraTerrain::Height>;

bool IsOpen(EAethraBlock Block)
{
    return Block == EAethraBlock::Air || AethraTerrain::IsPlant(Block);
}

bool IsTreeBlock(EAethraBlock Block)
{
    return Block == EAethraBlock::Wood || Block == EAethraBlock::BirchWood || AethraTerrain::IsLeaves(Block);
}

// Standing cell on the generated ground of a column, looking through trees and plants; -1 = water or none.
int32 GroundStand(const FGenColumn& Column)
{
    for (int32 Z = AethraTerrain::Height - 1; Z > 0; --Z)
    {
        const EAethraBlock Block = Column[Z];
        if (IsOpen(Block) || IsTreeBlock(Block))
        {
            continue;
        }
        return Block == EAethraBlock::Water ? -1 : Z + 1;
    }
    return -1;
}

FIntPoint Rotate(int32 Facing, int32 X, int32 Y)
{
    switch (Facing & 3)
    {
    case 1: return FIntPoint(-Y, X);
    case 2: return FIntPoint(-X, -Y);
    case 3: return FIntPoint(Y, -X);
    default: return FIntPoint(X, Y);
    }
}

int32 MaxSlope(EAethraSite Kind)
{
    return Kind == EAethraSite::Square || Kind == EAethraSite::RestArea ? 1 : 2; // farm beds go only where it is level
}

// Cells above the floor that must be open before building (walls and roof).
int32 Headroom(EAethraSite Kind)
{
    switch (Kind)
    {
    case EAethraSite::House: return 4;
    case EAethraSite::Storage:
    case EAethraSite::Workshop:
    case EAethraSite::Market: return 3;
    default: return 1;
    }
}

const TCHAR* SiteName(EAethraSite Kind)
{
    switch (Kind)
    {
    case EAethraSite::Square: return TEXT("praça");
    case EAethraSite::House: return TEXT("casa");
    case EAethraSite::Farm: return TEXT("fazenda");
    case EAethraSite::Workshop: return TEXT("oficina");
    case EAethraSite::Storage: return TEXT("depósito");
    case EAethraSite::Market: return TEXT("mercado");
    case EAethraSite::RestArea: return TEXT("área de descanso");
    default: return TEXT("?");
    }
}
}

FIntPoint FAethraLisVillage::Size(EAethraSite Kind)
{
    switch (Kind)
    {
    case EAethraSite::Square: return FIntPoint(3, 3);
    case EAethraSite::House: return FIntPoint(5, 5);
    case EAethraSite::Farm: return FIntPoint(7, 7);
    case EAethraSite::Workshop: return FIntPoint(5, 4);
    case EAethraSite::Storage: return FIntPoint(5, 4);
    case EAethraSite::Market: return FIntPoint(3, 3);
    case EAethraSite::RestArea: return FIntPoint(5, 3);
    default: return FIntPoint(1, 1);
    }
}

FIntPoint FAethraLisVillage::ToWorld(const FLisSite& Site, int32 X, int32 Y)
{
    return FIntPoint(Site.Cell.X, Site.Cell.Y) + Rotate(Site.Facing, X, Y);
}

FIntVector FAethraLisVillage::Local(const FLisSite& Site, int32 X, int32 Y, int32 Z) const
{
    const FIntPoint P = ToWorld(Site, X, Y);
    return FIntVector(P.X, P.Y, Site.Cell.Z + Z);
}

TArray<FLisSite> FAethraLisVillage::Plan(const AAethraVoxelWorld& World, FAethraLisNav& Nav, FIntPoint Center, FString& OutReport, bool bClearTrees)
{
    TArray<FLisSite> Result;
    TSet<FIntPoint> Reserved;
    TMap<FIntPoint, bool> UntouchedCache;
    TMap<FIntPoint, FGenColumn> GenCache;
    auto Gen = [&](FIntPoint P) -> const FGenColumn&
    {
        if (const FGenColumn* Known = GenCache.Find(P))
        {
            return *Known;
        }
        FGenColumn& Column = GenCache.Add(P);
        AethraTerrain::GenerateColumn(P, World.Seed, Column);
        return Column;
    };
    TArray<EAethraBlock> Current;
    int32 Rejected[5] = {}; // why candidates failed: bounds/reserved, tree/water, edited, slope, canopy

    // The player never edited this column: what is there is exactly what the terrain generated.
    auto Untouched = [&](FIntPoint P)
    {
        if (const bool* Known = UntouchedCache.Find(P))
        {
            return *Known;
        }
        const FGenColumn& Generated = Gen(P);
        World.GetColumn(P, Current);
        bool bSame = true;
        for (int32 Z = 0; Z < AethraTerrain::Height && bSame; ++Z)
        {
            bSame = Current[Z] == Generated[Z];
        }
        UntouchedCache.Add(P, bSame);
        return bSame;
    };

    // Footprint plus a one-cell margin (and the door apron) on fairly flat, clear, untouched ground.
    auto Fits = [&](FLisSite& Site, int32 Margin) -> bool
    {
        const EAethraSite Kind = static_cast<EAethraSite>(Site.Kind);
        const FIntPoint S = Size(Kind);
        int32 Low = MAX_int32, High = MIN_int32;
        for (int32 Y = -Margin - 1; Y < S.Y + Margin; ++Y)
        {
            for (int32 X = -Margin; X < S.X + Margin; ++X)
            {
                const FIntPoint P = ToWorld(Site, X, Y);
                const bool bInside = X >= 0 && Y >= 0 && X < S.X && Y < S.Y;
                const bool bApron = Y == -1 && X >= 0 && X < S.X; // the walkway in front of the door
                if (!Nav.InBounds(P) || Reserved.Contains(P))
                {
                    ++Rejected[0];
                    return false;
                }
                if (!bInside && !bApron)
                {
                    continue; // the margin only keeps plots apart; trees may grow there
                }
                if (!Untouched(P))
                {
                    ++Rejected[2];
                    return false;
                }
                int32 Ground;
                if (bClearTrees)
                {
                    Ground = GroundStand(Gen(P)); // generated trees on the plot will be cleared
                }
                else
                {
                    const FAethraNavColumn& C = Nav.Column(P);
                    Ground = Nav.Walkable(P) && !C.bWater && C.TrunkBaseZ < 0 ? C.StandZ : -1;
                }
                if (Ground < 0)
                {
                    ++Rejected[1];
                    return false;
                }
                if (bInside)
                {
                    Low = FMath::Min(Low, Ground);
                    High = FMath::Max(High, Ground);
                }
            }
        }
        if (High - Low > MaxSlope(Kind))
        {
            ++Rejected[3];
            return false;
        }
        // The door: stairs go down from it one block per step and must meet untouched ground within six
        // steps (no trunk in the way unless trees may be felled); ground above the floor is refused.
        bool bMeets = false;
        for (int32 K = 1; K <= 6 && !bMeets; ++K)
        {
            const FIntPoint P = ToWorld(Site, S.X / 2, -K);
            if (!Nav.InBounds(P) || !Untouched(P) || (K > 1 && Reserved.Contains(P)))
            {
                break;
            }
            const int32 Ground = bClearTrees ? GroundStand(Gen(P)) : (Nav.Column(P).TrunkBaseZ < 0 ? Nav.Column(P).StandZ : -1);
            if (Ground < 0)
            {
                break;
            }
            if (Ground > High)
            {
                break; // walking down through a 2-block doorway needs a third block of head room
            }
            bMeets = Ground >= High - K + 1; // the stair top here stands at High - K + 1
        }
        if (!bMeets)
        {
            ++Rejected[3];
            return false;
        }
        for (int32 Y = 0; Y < S.Y; ++Y)
        {
            for (int32 X = 0; X < S.X; ++X)
            {
                const FGenColumn& Column = Gen(ToWorld(Site, X, Y));
                for (int32 Z = GroundStand(Column); Z <= High + Headroom(Kind); ++Z)
                {
                    if (!IsOpen(Column[Z]) && !(bClearTrees && IsTreeBlock(Column[Z])))
                    {
                        ++Rejected[4];
                        return false; // canopy or overhang above the plot
                    }
                }
            }
        }
        Site.Cell.Z = High;
        return true;
    };
    auto Reserve = [&](const FLisSite& Site, int32 Margin)
    {
        const FIntPoint S = Size(static_cast<EAethraSite>(Site.Kind));
        for (int32 Y = -Margin - 1; Y < S.Y + Margin; ++Y)
        {
            for (int32 X = -Margin; X < S.X + Margin; ++X)
            {
                Reserved.Add(ToWorld(Site, X, Y));
            }
        }
    };
    auto MakeSite = [](EAethraSite Kind, FIntPoint CentreAt, int32 Facing)
    {
        const FIntPoint S = Size(Kind);
        FLisSite Site;
        Site.Kind = static_cast<uint8>(Kind);
        Site.Facing = static_cast<uint8>(Facing);
        const FIntPoint Origin = CentreAt - Rotate(Facing, S.X / 2, S.Y / 2);
        Site.Cell = FIntVector(Origin.X, Origin.Y, 0);
        return Site;
    };

    // 1. Where: the most open ground near Center. An openness map (walkable, no trunk, no water, no
    //    canopy just above) summed over 17x17 windows with an integral image; distance costs a little.
    constexpr int32 Span = 60, Window = 8;
    const int32 Side = 2 * Span + 1;
    TArray<int32> Sum;
    Sum.SetNumZeroed((Side + 1) * (Side + 1));
    for (int32 Y = 0; Y < Side; ++Y)
    {
        for (int32 X = 0; X < Side; ++X)
        {
            const FIntPoint P = Center + FIntPoint(X - Span, Y - Span);
            bool bOpen = Nav.InBounds(P);
            if (bOpen && bClearTrees)
            {
                bOpen = GroundStand(Gen(P)) >= 0; // dry land; trees do not matter here
            }
            else if (bOpen)
            {
                bOpen = Nav.Walkable(P);
                const FAethraNavColumn& C = Nav.Column(P);
                bOpen = bOpen && !C.bWater && C.TrunkBaseZ < 0 && C.IsOpen(C.StandZ + 2) && C.IsOpen(C.StandZ + 3) && C.IsOpen(C.StandZ + 4);
            }
            Sum[(Y + 1) * (Side + 1) + X + 1] = bOpen + Sum[Y * (Side + 1) + X + 1] + Sum[(Y + 1) * (Side + 1) + X] - Sum[Y * (Side + 1) + X];
        }
    }
    auto OpenAround = [&](int32 X, int32 Y) // X, Y in map coordinates, window fully inside
    {
        const int32 X0 = X - Window, Y0 = Y - Window, X1 = X + Window + 1, Y1 = Y + Window + 1;
        return Sum[Y1 * (Side + 1) + X1] - Sum[Y0 * (Side + 1) + X1] - Sum[Y1 * (Side + 1) + X0] + Sum[Y0 * (Side + 1) + X0];
    };
    TArray<TPair<float, FIntPoint>> Centres;
    for (int32 Y = Window; Y < Side - Window; Y += 2)
    {
        for (int32 X = Window; X < Side - Window; X += 2)
        {
            const float Distance = FVector2D(X - Span, Y - Span).Size();
            Centres.Add({ OpenAround(X, Y) - .6f * Distance, Center + FIntPoint(X - Span, Y - Span) });
        }
    }
    Centres.Sort([](const TPair<float, FIntPoint>& A, const TPair<float, FIntPoint>& B) { return A.Key > B.Key; });

    // 2. The square: near the best open areas, flat and clear.
    FLisSite Square;
    bool bSquare = false;
    for (int32 I = 0; I < Centres.Num() && I < 40 && !bSquare; ++I)
    {
        for (int32 R = 0; R <= 4 && !bSquare; ++R)
        {
            for (int32 Y = -R; Y <= R && !bSquare; ++Y)
            {
                for (int32 X = -R; X <= R && !bSquare; ++X)
                {
                    if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) == R)
                    {
                        Square = MakeSite(EAethraSite::Square, Centres[I].Value + FIntPoint(X, Y), 0);
                        bSquare = Fits(Square, 2);
                    }
                }
            }
        }
    }
    auto Why = [&]()
    {
        return FString::Printf(TEXT("candidatos recusados: %d fora/ocupado, %d árvore/água, %d editado pelo jogador, %d inclinado, %d sob copa"),
            Rejected[0], Rejected[1], Rejected[2], Rejected[3], Rejected[4]);
    };
    if (!bSquare)
    {
        OutReport = TEXT("Vila: nenhum terreno plano e intocado perto do centro; usando os pontos do LIS 02 (") + Why() + TEXT(").");
        return Result;
    }
    Square.Owner = bClearTrees ? 1 : 0; // remembered: this village may clear generated trees on its plots
    Reserve(Square, 2);
    Result.Add(Square);
    const FIntPoint Plaza = ToWorld(Square, 1, 1);
    const FIntVector PlazaStand = FIntVector(Plaza.X, Plaza.Y, Square.Cell.Z);
    FIntVector Walk;
    if (!Nav.NearestStand(PlazaStand + FIntVector(0, -2, 0), 2, Walk))
    {
        Walk = Nav.Stand(Plaza + FIntPoint(0, -2));
    }

    // 3. The rest, in rings around the square, each facing it and reachable from it.
    struct FWant { EAethraSite Kind; int32 Count; int32 MinR; int32 MaxR; };
    const FWant Wanted[] = {
        { EAethraSite::Market, 1, 5, 11 }, { EAethraSite::Storage, 1, 6, 14 }, { EAethraSite::Workshop, 1, 6, 14 },
        { EAethraSite::RestArea, 1, 5, 14 }, { EAethraSite::House, Houses, 8, 28 }, { EAethraSite::Farm, 1, 10, 32 } };
    int32 Missing = 0;
    for (const FWant& Want : Wanted)
    {
        for (int32 N = 0; N < Want.Count; ++N)
        {
            bool bPlaced = false;
            for (int32 R = Want.MinR; R <= Want.MaxR && !bPlaced; ++R)
            {
                for (int32 Step = 0; Step < 36 && !bPlaced; ++Step)
                {
                    const float Angle = (Step * 10.f + 37.f * static_cast<int32>(Want.Kind) + 71.f * N) * PI / 180.f;
                    const FIntPoint At = Plaza + FIntPoint(FMath::RoundToInt(R * FMath::Cos(Angle)), FMath::RoundToInt(R * FMath::Sin(Angle)));
                    // Face the square: pick the facing whose front points most toward it.
                    const FVector2D ToPlaza = FVector2D(Plaza - At).GetSafeNormal();
                    int32 Facing = 0;
                    float BestDot = -2.f;
                    for (int32 F = 0; F < 4; ++F)
                    {
                        const FIntPoint Front = Rotate(F, 0, -1);
                        const float Dot = FVector2D::DotProduct(FVector2D(Front), ToPlaza);
                        if (Dot > BestDot)
                        {
                            BestDot = Dot;
                            Facing = F;
                        }
                    }
                    FLisSite Site = MakeSite(Want.Kind, At, Facing);
                    if (!Fits(Site, 1))
                    {
                        continue;
                    }
                    // Reachable from the square on today's terrain: to the door, or (when trees still
                    // stand on the plot) to a cell next to it.
                    const FIntPoint Apron = ToWorld(Site, Size(Want.Kind).X / 2, -1);
                    FIntVector Target;
                    TArray<FIntVector> Path;
                    if (!Nav.NearestStand(FIntVector(Apron.X, Apron.Y, Site.Cell.Z), bClearTrees ? 2 : 0, Target)
                        || !Nav.FindPath(Walk, Target, Path, 6000))
                    {
                        continue;
                    }
                    Site.Owner = Want.Kind == EAethraSite::House ? N * 4 + 1 : 0;
                    Reserve(Site, 1);
                    Result.Add(Site);
                    bPlaced = true;
                }
            }
            Missing += !bPlaced;
        }
    }
    // A village needs its core: market, storage, workshop, farm and at least three houses. Without them
    // nothing is built (no half village in someone's world) and the LIS 02 anchors are used.
    auto Count = [&Result](EAethraSite Kind) { return Result.FilterByPredicate([Kind](const FLisSite& S) { return S.Kind == static_cast<uint8>(Kind); }).Num(); };
    const bool bCore = Count(EAethraSite::Market) && Count(EAethraSite::Storage) && Count(EAethraSite::Workshop) && Count(EAethraSite::Farm)
        && Count(EAethraSite::House) >= 3;
    OutReport = FString::Printf(TEXT("Vila: %d locais planejados ao redor de (%d, %d, %d)%s; %s%s."), Result.Num(), Plaza.X, Plaza.Y, Square.Cell.Z,
        Missing ? *FString::Printf(TEXT(", %d sem espaço"), Missing) : TEXT(""), *Why(),
        bCore ? TEXT("") : *FString::Printf(TEXT(". Espaço insuficiente para o núcleo da vila (mercado %d, depósito %d, oficina %d, fazenda %d, casas %d): nada será construído"),
            Count(EAethraSite::Market), Count(EAethraSite::Storage), Count(EAethraSite::Workshop), Count(EAethraSite::Farm), Count(EAethraSite::House)));
    if (!bCore)
    {
        Result.Reset();
    }
    return Result;
}

void FAethraLisVillage::Setup(const AAethraVoxelWorld& World, const TArray<FLisSite>& InSites, FAethraLisNav* Nav)
{
    Sites = InSites;
    Seed = World.Seed;
    Ops.Reset();
    Expected.Reset();
    Crops.Reset();
    Next = 0;
    TMap<FIntPoint, FGenColumn> Generated;
    auto Gen = [&](FIntPoint P) -> const FGenColumn&
    {
        if (const FGenColumn* Known = Generated.Find(P))
        {
            return *Known;
        }
        FGenColumn& Column = Generated.Add(P);
        AethraTerrain::GenerateColumn(P, Seed, Column);
        return Column;
    };

    bClearTrees = Sites.Num() && Sites[0].Kind == static_cast<uint8>(EAethraSite::Square) && Sites[0].Owner == 1;
    ClearedTrees.Reset();
    TSet<FIntVector> Clearing;
    auto Free = [this](EAethraBlock Block) { return IsOpen(Block) || (bClearTrees && IsTreeBlock(Block)); };
    // Demo village: whole generated trees standing on a plot or its margin go first, then any tree
    // block left above a plot (overhanging crowns). Recorded so Remove() can grow them back.
    if (bClearTrees)
    {
        for (const FLisSite& Site : Sites)
        {
            const EAethraSite Kind = static_cast<EAethraSite>(Site.Kind);
            if (Kind == EAethraSite::Built)
            {
                continue;
            }
            const FIntPoint S = Size(Kind);
            for (int32 Y = -7; Y < S.Y + 1; ++Y)
            {
                for (int32 X = -1; X < S.X + 1; ++X)
                {
                    const bool bStairs = Y < -1 && X == S.X / 2; // the stair line in front of the door
                    if (Y < -2 && !bStairs)
                    {
                        continue;
                    }
                    const FIntPoint P = ToWorld(Site, X, Y);
                    const FGenColumn& Column = Gen(P);
                    const bool bOnPlot = (X >= 0 && Y >= -1 && X < S.X && Y < S.Y) || bStairs;
                    bool bTrunk = false;
                    for (int32 Z = 1; Z < AethraTerrain::Height; ++Z)
                    {
                        bTrunk |= Column[Z] == EAethraBlock::Wood || Column[Z] == EAethraBlock::BirchWood;
                        if (bOnPlot && IsTreeBlock(Column[Z]))
                        {
                            Clearing.Add(FIntVector(P.X, P.Y, Z));
                        }
                    }
                    if (!bTrunk)
                    {
                        continue;
                    }
                    for (int32 DY = -2; DY <= 2; ++DY) // the crown of a felled tree
                    {
                        for (int32 DX = -2; DX <= 2; ++DX)
                        {
                            const FIntPoint Q = P + FIntPoint(DX, DY);
                            const FGenColumn& Crown = Gen(Q);
                            for (int32 Z = 1; Z < AethraTerrain::Height; ++Z)
                            {
                                if (IsTreeBlock(Crown[Z]) && (DX || DY ? AethraTerrain::IsLeaves(Crown[Z]) : true))
                                {
                                    Clearing.Add(FIntVector(Q.X, Q.Y, Z));
                                }
                            }
                        }
                    }
                }
            }
        }
        for (const FIntVector& Cell : Clearing)
        {
            Ops.Add({ Cell, EAethraBlock::Air, true });
            ClearedTrees.Add(Cell);
        }
    }

    for (const FLisSite& Site : Sites)
    {
        const EAethraSite Kind = static_cast<EAethraSite>(Site.Kind);
        if (Kind == EAethraSite::Built)
        {
            continue;
        }
        const FIntPoint S = Size(Kind);
        auto Put = [&](int32 X, int32 Y, int32 Z, EAethraBlock Block)
        {
            const FIntVector Cell = Local(Site, X, Y, Z);
            Expected.Add(Cell, Block);
            Ops.Add({ Cell, Block });
        };
        auto Clear = [&](int32 X, int32 Y, int32 Z) { Ops.Add({ Local(Site, X, Y, Z), EAethraBlock::Air }); };
        // Stone under the floor wherever the ground is lower (from the generated terrain: reproducible).
        auto Foundation = [&](EAethraBlock Fill = EAethraBlock::Stone)
        {
            for (int32 Y = 0; Y < S.Y; ++Y)
            {
                for (int32 X = 0; X < S.X; ++X)
                {
                    const FGenColumn& Column = Gen(ToWorld(Site, X, Y));
                    for (int32 Z = Site.Cell.Z - 1; Z > 0 && Free(Column[Z]); --Z)
                    {
                        Put(X, Y, Z - Site.Cell.Z, Fill);
                    }
                }
            }
        };
        auto Step = [&]()
        {
            // Stone stairs down from the door until they meet the ground (one block per step).
            for (int32 K = 1; K <= 6; ++K)
            {
                const FGenColumn& Column = Gen(ToWorld(Site, S.X / 2, -K));
                const int32 Top = -K; // this step's block, relative to the floor
                bool bGround = false;
                for (int32 Z = Site.Cell.Z + Top; Z > 0 && Free(Column[Z]); --Z)
                {
                    Put(S.X / 2, -K, Z - Site.Cell.Z, EAethraBlock::Stone);
                    bGround = true;
                }
                if (!bGround)
                {
                    break; // the ground already reaches this step
                }
            }
        };
        auto Walls = [&](int32 Top, bool bDoor, bool bWindows)
        {
            for (int32 Y = 0; Y < S.Y; ++Y)
            {
                for (int32 X = 0; X < S.X; ++X)
                {
                    const bool bEdgeX = X == 0 || X == S.X - 1, bEdgeY = Y == 0 || Y == S.Y - 1;
                    for (int32 Z = 0; Z <= Top; ++Z)
                    {
                        if (!bEdgeX && !bEdgeY)
                        {
                            Clear(X, Y, Z);
                        }
                        else if (bEdgeX && bEdgeY)
                        {
                            Put(X, Y, Z, EAethraBlock::Wood); // log corner posts
                        }
                        else if (bDoor && Y == 0 && X == S.X / 2 && Z <= 1)
                        {
                            Clear(X, Y, Z);
                        }
                        else if (bWindows && Z == 1 && ((Y == S.Y - 1 && X == S.X / 2) || (bEdgeX && Y == S.Y / 2)))
                        {
                            Clear(X, Y, Z);
                        }
                        else
                        {
                            Put(X, Y, Z, EAethraBlock::BirchWood);
                        }
                    }
                }
            }
        };
        auto Roof = [&](int32 Z, int32 Inset, EAethraBlock Block)
        {
            for (int32 Y = Inset; Y < S.Y - Inset; ++Y)
            {
                for (int32 X = Inset; X < S.X - Inset; ++X)
                {
                    Put(X, Y, Z, Block);
                }
            }
        };

        switch (Kind)
        {
        case EAethraSite::House:
            Foundation();
            Walls(2, true, true);
            Roof(3, 0, EAethraBlock::Wood);
            Roof(4, 1, EAethraBlock::Wood);
            Step();
            break;
        case EAethraSite::Storage:
            Foundation();
            Walls(2, true, false);
            Roof(3, 0, EAethraBlock::Wood);
            Put(1, 2, 0, EAethraBlock::Wood); // stacked logs
            Put(3, 2, 0, EAethraBlock::Wood);
            Step();
            break;
        case EAethraSite::Workshop:
            Foundation();
            for (int32 Y = 0; Y < S.Y; ++Y)
            {
                for (int32 X = 0; X < S.X; ++X)
                {
                    const bool bPost = (X == 0 || X == S.X - 1) && (Y == 0 || Y == S.Y - 1);
                    for (int32 Z = 0; Z <= 2; ++Z)
                    {
                        if (bPost)
                        {
                            Put(X, Y, Z, EAethraBlock::Wood);
                        }
                        else if (!(Z == 0 && Y == S.Y - 1 && (X == 1 || X == 2)))
                        {
                            Clear(X, Y, Z);
                        }
                    }
                }
            }
            Put(2, S.Y - 1, 0, EAethraBlock::Wood);  // workbench
            Put(1, S.Y - 1, 0, EAethraBlock::Stone); // anvil
            Roof(3, 0, EAethraBlock::Wood);
            Step();
            break;
        case EAethraSite::Market:
            Foundation();
            for (int32 Y = 0; Y < S.Y; ++Y)
            {
                for (int32 X = 0; X < S.X; ++X)
                {
                    const bool bPost = (X == 0 || X == S.X - 1) && (Y == 0 || Y == S.Y - 1);
                    for (int32 Z = 0; Z <= 2; ++Z)
                    {
                        if (bPost)
                        {
                            Put(X, Y, Z, EAethraBlock::Wood);
                        }
                        else if (!(X == 1 && Y == 0 && Z == 0))
                        {
                            Clear(X, Y, Z);
                        }
                    }
                }
            }
            Put(1, 0, 0, EAethraBlock::BirchWood); // counter
            Roof(3, 0, EAethraBlock::BirchLeaves); // canopy
            Step();
            break;
        case EAethraSite::Farm:
            Foundation(EAethraBlock::Dirt); // level beds: soil only where the ground was lower
            for (int32 Y = 0; Y < S.Y; ++Y)
            {
                for (int32 X = 0; X < S.X; ++X)
                {
                    const bool bEdge = X == 0 || Y == 0 || X == S.X - 1 || Y == S.Y - 1;
                    const bool bGate = Y == 0 && (X == S.X / 2 || X == S.X / 2 - 1);
                    const FGenColumn& Column = Gen(ToWorld(Site, X, Y));
                    const EAethraBlock Below = Column[Site.Cell.Z - 1];
                    const bool bSoil = Free(Below) || Below == EAethraBlock::Grass || Below == EAethraBlock::Dirt;
                    if (bEdge && !bGate)
                    {
                        if (Free(Column[Site.Cell.Z]))
                        {
                            Put(X, Y, 0, EAethraBlock::BirchWood); // low fence
                        }
                    }
                    else if (!bEdge && Y % 2 == 1 && bSoil && Free(Column[Site.Cell.Z]))
                    {
                        Put(X, Y, 0, EAethraBlock::TallGrass); // crop rows (stand-in crop)
                        Crops.Add(Local(Site, X, Y, 0));
                    }
                    else
                    {
                        Clear(X, Y, 0); // walkways
                    }
                }
            }
            break;
        case EAethraSite::Square:
            Foundation();
            for (int32 Y = 0; Y < 3; ++Y)
            {
                for (int32 X = 0; X < 3; ++X)
                {
                    Put(X, Y, 0, X == 1 && Y == 1 ? EAethraBlock::Water : EAethraBlock::Stone); // well
                }
            }
            break;
        case EAethraSite::RestArea:
            Foundation();
            for (int32 Y = 0; Y < S.Y; ++Y)
            {
                for (int32 X = 0; X < S.X; ++X)
                {
                    Clear(X, Y, 0);
                    Clear(X, Y, 1);
                }
            }
            Put(0, 2, 0, EAethraBlock::BirchWood); // bench
            Put(1, 2, 0, EAethraBlock::BirchWood);
            Put(3, 0, 0, EAethraBlock::Stone);     // fire pit ring
            Put(2, 1, 0, EAethraBlock::Stone);
            Put(4, 1, 0, EAethraBlock::Stone);
            Put(3, 2, 0, EAethraBlock::Stone);
            break;
        default:
            break;
        }
    }

    // Trodden paths: vegetation cleared from the square to every door (only when planning; after a
    // restart the paths are already there and the lookup above is all that is needed).
    if (Nav && Sites.Num())
    {
        const FLisSite& Square = Sites[0];
        FIntVector From;
        if (Nav->NearestStand(Local(Square, 1, -1, 0), 2, From))
        {
            for (const FLisSite& Site : Sites)
            {
                const EAethraSite Kind = static_cast<EAethraSite>(Site.Kind);
                if (Kind == EAethraSite::Square || Kind == EAethraSite::Built)
                {
                    continue;
                }
                FIntVector To;
                TArray<FIntVector> Path;
                if (Nav->NearestStand(DoorSpot(Site), 2, To) && Nav->FindPath(From, To, Path, 4000))
                {
                    for (const FIntVector& Cell : Path)
                    {
                        if (!Expected.Contains(Cell) && AethraTerrain::IsPlant(Gen(FIntPoint(Cell.X, Cell.Y))[Cell.Z]))
                        {
                            Ops.Add({ Cell, EAethraBlock::Air });
                        }
                    }
                }
            }
        }
    }
}

bool FAethraLisVillage::IsBuilt() const
{
    return Sites.ContainsByPredicate([](const FLisSite& S) { return S.Kind == static_cast<uint8>(EAethraSite::Built); });
}

int32 FAethraLisVillage::Build(AAethraVoxelWorld& World, int32 Budget, bool& bDone)
{
    int32 Count = 0;
    TMap<FIntPoint, FGenColumn> Generated;
    while (Next < Ops.Num() && Count < Budget)
    {
        const FAethraVillageOp& Op = Ops[Next++];
        const EAethraBlock Now = World.GetBlock(Op.Cell);
        if (Now == Op.Block)
        {
            continue;
        }
        bool bFree = Now == EAethraBlock::Air;
        if (!bFree && (AethraTerrain::IsPlant(Now) || (Op.bTree && IsTreeBlock(Now))))
        {
            // Only vegetation the terrain generated may be replaced (trees only in the demo village);
            // anything the player put there stays.
            const FIntPoint P(Op.Cell.X, Op.Cell.Y);
            FGenColumn* Column = Generated.Find(P);
            if (!Column)
            {
                Column = &Generated.Add(P);
                AethraTerrain::GenerateColumn(P, Seed, *Column);
            }
            bFree = (*Column)[Op.Cell.Z] == Now;
        }
        if (bFree && World.SetBlockBy(Op.Cell, Op.Block, FAethraInstigator::Environment(EnvironmentId)))
        {
            ++Placed;
            ++Count;
            TreesCleared += Op.bTree;
        }
        else if (Op.Block != EAethraBlock::Air)
        {
            ++Skipped; // an existing block stays where the plan wanted one
        }
    }
    bDone = Next >= Ops.Num();
    return Count;
}

int32 FAethraLisVillage::Remove(AAethraVoxelWorld& World)
{
    int32 Restored = 0;
    for (const TPair<FIntVector, EAethraBlock>& Entry : Expected)
    {
        if (World.GetBlock(Entry.Key) != Entry.Value)
        {
            continue; // changed since (player or villager): leave it
        }
        FGenColumn Column;
        AethraTerrain::GenerateColumn(FIntPoint(Entry.Key.X, Entry.Key.Y), Seed, Column);
        Restored += World.SetBlockBy(Entry.Key, Column[Entry.Key.Z], FAethraInstigator::Environment(EnvironmentId));
    }
    // Trees cleared for the plots grow back where the cell is still empty.
    for (const FIntVector& Cell : ClearedTrees)
    {
        if (World.GetBlock(Cell) == EAethraBlock::Air)
        {
            FGenColumn Column;
            AethraTerrain::GenerateColumn(FIntPoint(Cell.X, Cell.Y), Seed, Column);
            Restored += World.SetBlockBy(Cell, Column[Cell.Z], FAethraInstigator::Environment(EnvironmentId));
        }
    }
    return Restored;
}

const FLisSite* FAethraLisVillage::FindSite(EAethraSite Kind, int32 Index) const
{
    for (const FLisSite& Site : Sites)
    {
        if (Site.Kind == static_cast<uint8>(Kind) && Index-- == 0)
        {
            return &Site;
        }
    }
    return nullptr;
}

FIntVector FAethraLisVillage::SquareSpot(int32 Slot) const
{
    const FLisSite* Site = FindSite(EAethraSite::Square);
    if (!Site)
    {
        return FIntVector::ZeroValue;
    }
    const float Angle = Slot * 2.39996f;
    return Local(*Site, 1 + FMath::RoundToInt(2.6f * FMath::Cos(Angle)), 1 + FMath::RoundToInt(2.6f * FMath::Sin(Angle)), 0);
}

FIntVector FAethraLisVillage::HomeSpot(int32 NpcId) const
{
    const FLisSite* Site = FindSite(EAethraSite::House, HouseOf(NpcId));
    if (!Site)
    {
        Site = FindSite(EAethraSite::House);
    }
    if (!Site)
    {
        return SquareSpot(NpcId);
    }
    static const FIntPoint Beds[] = { { 1, 3 }, { 3, 3 }, { 1, 2 }, { 3, 2 } };
    const FIntPoint Bed = Beds[(NpcId - 1) % 4];
    return Local(*Site, Bed.X, Bed.Y, 0);
}

FIntVector FAethraLisVillage::DoorSpot(const FLisSite& Site) const
{
    return Local(Site, Size(static_cast<EAethraSite>(Site.Kind)).X / 2, -1, 0);
}

FIntVector FAethraLisVillage::WorkbenchSpot(int32 Slot) const
{
    const FLisSite* Site = FindSite(EAethraSite::Workshop);
    static const int32 Places[] = { 2, 1, 3 };
    return Site ? Local(*Site, Places[FMath::Abs(Slot) % 3], 2, 0) : SquareSpot(Slot);
}

FIntVector FAethraLisVillage::WorkbenchTarget() const
{
    const FLisSite* Site = FindSite(EAethraSite::Workshop);
    return Site ? Local(*Site, 2, 3, 0) : SquareSpot(0);
}

FIntVector FAethraLisVillage::StorageSpot() const
{
    const FLisSite* Site = FindSite(EAethraSite::Storage);
    return Site ? Local(*Site, 2, 1, 0) : SquareSpot(1);
}

FIntVector FAethraLisVillage::StallSpot(bool bSeller, int32 Slot) const
{
    const FLisSite* Site = FindSite(EAethraSite::Market);
    if (!Site)
    {
        return SquareSpot(Slot);
    }
    return bSeller ? Local(*Site, 1, 1, 0) : Local(*Site, Slot % 3, -1 - Slot / 3 % 2, 0);
}

FIntVector FAethraLisVillage::RestSpot(int32 Slot) const
{
    const FLisSite* Site = FindSite(EAethraSite::RestArea);
    if (!Site)
    {
        return SquareSpot(Slot);
    }
    static const FIntPoint Seats[] = { { 0, 1 }, { 1, 1 }, { 2, 0 }, { 4, 0 }, { 4, 2 }, { 2, 2 } };
    const FIntPoint Seat = Seats[Slot % UE_ARRAY_COUNT(Seats)];
    return Local(*Site, Seat.X, Seat.Y, 0);
}

FIntVector FAethraLisVillage::FireSpot() const
{
    const FLisSite* Site = FindSite(EAethraSite::RestArea);
    return Site ? Local(*Site, 3, 1, 0) : SquareSpot(0);
}
