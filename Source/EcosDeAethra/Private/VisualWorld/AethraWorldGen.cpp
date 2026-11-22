#include "VisualWorld/AethraWorldGen.h"

#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"

namespace
{
uint32 WorldMix(uint32 H)
{
    H ^= H >> 16;
    H *= 0x7feb352dU;
    H ^= H >> 15;
    H *= 0x846ca68bU;
    H ^= H >> 16;
    return H;
}

float Smooth(float A, float B, float X)
{
    const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f);
    return T * T * (3.f - 2.f * T);
}

constexpr float RiverThreshold = 400.f;   // nodes draining into a node before it is a stream (~10 ha)
constexpr float MainThreshold = 2600.f;   // ... before it is a river body of its own
constexpr int32 SeaBody = 0, RiverBody = 1, StreamBody = 2, WaterfallBody = 3, FirstLakeBody = 4;
}

FAethraWorldGen::FAethraWorldGen(int32 InSeed)
    : Seed(InSeed)
{
    const double Begin = FPlatformTime::Seconds();
    for (int32 I = 0; I < UE_ARRAY_COUNT(Offsets); ++I)
    {
        const uint32 H = WorldMix(static_cast<uint32>(Seed) * 2654435761U + I * 97U);
        Offsets[I] = FVector2D((H & 0xFFFF) / 37.3, (H >> 16) / 41.7);
    }
    BuildDrainage();
    InitMs = (FPlatformTime::Seconds() - Begin) * 1000.0;
}

float FAethraWorldGen::Noise(FVector2D P) const
{
    const int32 X0 = FMath::FloorToInt(P.X), Y0 = FMath::FloorToInt(P.Y);
    const float FX = static_cast<float>(P.X - X0), FY = static_cast<float>(P.Y - Y0);
    auto Grad = [this](int32 X, int32 Y, float DX, float DY)
    {
        const uint32 H = WorldMix(static_cast<uint32>(Seed) ^ WorldMix(static_cast<uint32>(X) * 0x9E3779B1U ^ WorldMix(static_cast<uint32>(Y) * 0x85EBCA77U)));
        const float A = (H & 0xFFFF) * (2.f * PI / 65536.f);
        return FMath::Cos(A) * DX + FMath::Sin(A) * DY;
    };
    auto Fade = [](float T) { return T * T * T * (T * (T * 6.f - 15.f) + 10.f); };
    const float U = Fade(FX), V = Fade(FY);
    const float N00 = Grad(X0, Y0, FX, FY), N10 = Grad(X0 + 1, Y0, FX - 1.f, FY);
    const float N01 = Grad(X0, Y0 + 1, FX, FY - 1.f), N11 = Grad(X0 + 1, Y0 + 1, FX - 1.f, FY - 1.f);
    return 1.41f * FMath::Lerp(FMath::Lerp(N00, N10, U), FMath::Lerp(N01, N11, U), V);
}

float FAethraWorldGen::Fbm(FVector2D P, int32 Octaves) const
{
    float Sum = 0.f, Amp = .5f, Norm = 0.f;
    for (int32 O = 0; O < Octaves; ++O)
    {
        Sum += Amp * Noise(P);
        Norm += Amp;
        P = FVector2D(P.X * 2.03 + 17.1, P.Y * 2.03 - 9.3);
        Amp *= .5f;
    }
    return Sum / Norm;
}

float FAethraWorldGen::Ridged(FVector2D P, int32 Octaves) const
{
    float Sum = 0.f, Amp = .5f, Norm = 0.f, Weight = 1.f;
    for (int32 O = 0; O < Octaves; ++O)
    {
        float R = 1.f - FMath::Abs(Noise(P));
        R *= R * Weight;
        Weight = FMath::Clamp(R * 1.6f, 0.f, 1.f);
        Sum += Amp * R;
        Norm += Amp;
        P = FVector2D(P.X * 2.1 + 5.3, P.Y * 2.1 + 11.9);
        Amp *= .5f;
    }
    return Sum / Norm;
}

float FAethraWorldGen::BaseHeight(FVector2D P) const
{
    const float Dist = static_cast<float>(P.Size());
    // Continents: land around the spawn basin, sea where the large-scale field drops.
    const float C = Fbm(P / 2600.0 + Offsets[0], 4) + .45f * (1.f - Smooth(0.f, 3200.f, Dist));
    const float Land = Smooth(-.55f, -.3f, C);
    // Wide plains with gentle undulation, soft hills in patches.
    const float Plains = 32.f + 8.f * Fbm(P / 800.0 + Offsets[1], 4) + 2.2f * Fbm(P / 140.0 + Offsets[2], 3);
    const float Hills = Smooth(.05f, .5f, Fbm(P / 1200.0 + Offsets[3], 3)) * 24.f * (.6f + .4f * Fbm(P / 320.0 + Offsets[4], 3));
    // Mountain ranges: rare in the basin, rising beyond ~1.5 km so the horizon has peaks.
    const float Uplift = FMath::Clamp(.55f * Fbm(P / 2400.0 + Offsets[5], 3) + 1.15f * Smooth(1400.f, 3800.f, Dist), 0.f, 1.3f);
    const float Mountain = FMath::Pow(Uplift, 1.7f) * (Ridged(P / 750.0 + Offsets[6], 4) * 330.f + Ridged(P / 240.0 + Offsets[7], 3) * 70.f);
    const float SeaFloor = -18.f + 6.f * Fbm(P / 500.0 + Offsets[8], 2);
    return FMath::Lerp(SeaFloor, Plains + Hills + Mountain, Land);
}

void FAethraWorldGen::BuildDrainage()
{
    const int32 Count = Nodes * Nodes;
    Base.SetNumUninitialized(Count);
    Filled.SetNumUninitialized(Count);
    Accum.Init(1.f, Count);
    Down.Init(-1, Count);
    ParallelFor(Nodes, [this](int32 Y)
    {
        for (int32 X = 0; X < Nodes; ++X)
        {
            Base[Y * Nodes + X] = BaseHeight(NodePosition(FIntPoint(X, Y)));
        }
    });

    // 1. Priority flood from the outlets (border and sea): every node drains to an outlet; filled
    //    depressions get a tiny slope so their flats drain too (Barnes et al., epsilon variant).
    struct FItem { float H; int32 I; };
    TArray<FItem> Heap;
    TArray<uint8> Done;
    Done.Init(0, Count);
    auto Less = [](const FItem& A, const FItem& B) { return A.H < B.H; };
    for (int32 I = 0; I < Count; ++I)
    {
        const int32 X = I % Nodes, Y = I / Nodes;
        if (X == 0 || Y == 0 || X == Nodes - 1 || Y == Nodes - 1 || Base[I] < 0.f)
        {
            Filled[I] = Base[I];
            Done[I] = 1;
            Heap.HeapPush({ Base[I], I }, Less);
        }
    }
    static const FIntPoint Steps[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
    while (Heap.Num())
    {
        FItem Item;
        Heap.HeapPop(Item, Less);
        const int32 X = Item.I % Nodes, Y = Item.I / Nodes;
        for (const FIntPoint& S : Steps)
        {
            const int32 NX = X + S.X, NY = Y + S.Y;
            if (NX < 0 || NY < 0 || NX >= Nodes || NY >= Nodes)
            {
                continue;
            }
            const int32 N = NY * Nodes + NX;
            if (Done[N])
            {
                continue;
            }
            Done[N] = 1;
            Filled[N] = FMath::Max(Base[N], Filled[Item.I] + .002f);
            Heap.HeapPush({ Filled[N], N }, Less);
        }
    }

    // 2. Steepest descent on the filled surface (D8), then flow accumulation from high to low.
    TArray<int32> Order;
    Order.SetNumUninitialized(Count);
    for (int32 I = 0; I < Count; ++I)
    {
        Order[I] = I;
        const int32 X = I % Nodes, Y = I / Nodes;
        if (Base[I] < 0.f || X == 0 || Y == 0 || X == Nodes - 1 || Y == Nodes - 1)
        {
            continue; // outlets
        }
        float Best = 0.f;
        for (const FIntPoint& S : Steps)
        {
            const int32 N = (Y + S.Y) * Nodes + X + S.X;
            const float Drop = (Filled[I] - Filled[N]) / (S.X && S.Y ? 1.414f : 1.f);
            if (Drop > Best)
            {
                Best = Drop;
                Down[I] = N;
            }
        }
    }
    Order.Sort([this](int32 A, int32 B) { return Filled[A] > Filled[B]; });
    for (const int32 I : Order)
    {
        if (Down[I] >= 0)
        {
            Accum[Down[I]] += Accum[I];
        }
    }

    // 3. Water bodies. Sea first, the main river and streams, then each filled depression a lake.
    auto MakeBody = [this](const TCHAR* Name, EHydroKind Kind, const TCHAR* Preset)
    {
        FHydroBody B;
        B.Id = Bodies.Num();
        B.Name = Name;
        B.Kind = Kind;
        B.Props = FHydroGen::Preset(Preset);
        B.Optics = FHydroGen::ComputeOptics(B.Props);
        Bodies.Add(B);
    };
    MakeBody(TEXT("Mar"), EHydroKind::Sea, TEXT("TropicalSea"));
    // The main river's water depends on the seed: a green, algae-rich lowland river or a clear one.
    MakeBody(TEXT("Rio"), EHydroKind::River, (WorldMix(static_cast<uint32>(Seed) + 7U) & 1) ? TEXT("GreenRiver") : TEXT("ClearStream"));
    MakeBody(TEXT("Riachos"), EHydroKind::Stream, TEXT("ClearStream"));
    MakeBody(TEXT("Cachoeiras"), EHydroKind::Waterfall, TEXT("Waterfall"));
    if (Bodies[RiverBody].Props.CurrentSpeed > 1.f)
    {
        Bodies[RiverBody].Props.Depth = 2.5f; // a clear river is still a river, not a creek
        Bodies[RiverBody].Props.CurrentSpeed = .8f;
        Bodies[RiverBody].Optics = FHydroGen::ComputeOptics(Bodies[RiverBody].Props);
    }

    LakeBody.Init(-1, Count);
    LakeMask.Init(0.f, Count);
    TArray<float> LakeLevels;
    for (int32 I = 0; I < Count; ++I)
    {
        if (LakeBody[I] >= 0 || Base[I] < 0.f || Filled[I] - Base[I] < .6f)
        {
            continue;
        }
        // Flood the connected depression.
        TArray<int32> Stack = { I }, Members;
        LakeBody[I] = -2;
        float Level = Filled[I];
        while (Stack.Num())
        {
            const int32 C = Stack.Pop(EAllowShrinking::No);
            Members.Add(C);
            Level = FMath::Max(Level, Filled[C]);
            const int32 X = C % Nodes, Y = C / Nodes;
            for (const FIntPoint& S : Steps)
            {
                const int32 NX = X + S.X, NY = Y + S.Y;
                if (NX < 0 || NY < 0 || NX >= Nodes || NY >= Nodes)
                {
                    continue;
                }
                const int32 N = NY * Nodes + NX;
                if (LakeBody[N] == -1 && Base[N] >= 0.f && Filled[N] - Base[N] >= .6f)
                {
                    LakeBody[N] = -2;
                    Stack.Add(N);
                }
            }
        }
        const bool bLake = Members.Num() >= 10 && Bodies.Num() < FHydroGen::MaxBodies;
        const int32 Id = bLake ? Bodies.Num() : -3;
        if (bLake)
        {
            const float Mean = Base[I];
            // Upland lakes are cold and clear; lowland lakes collect organic matter and darken.
            MakeBody(*FString::Printf(TEXT("Lago %d"), Id - FirstLakeBody + 1), EHydroKind::Lake, Mean > 70.f ? TEXT("ClearLake") : TEXT("DarkLake"));
            LakeLevels.Add(Level);
            LakeNodes += Members.Num();
        }
        for (const int32 M : Members)
        {
            LakeBody[M] = Id;
        }
    }
    for (int32 I = 0; I < Count; ++I)
    {
        if (LakeBody[I] >= FirstLakeBody)
        {
            Filled[I] = LakeLevels[LakeBody[I] - FirstLakeBody]; // flat lake surface
        }
        LakeMask[I] = LakeBody[I] >= FirstLakeBody ? 1.f : 0.f;
        SeaNodes += Base[I] < 0.f;
    }

    // 4. Rivers and the distance transform to them (two-pass chamfer, nearest source kept).
    RiverDist.Init(1e9f, Count);
    RiverLevel.Init(0.f, Count);
    RiverWidth.Init(0.f, Count);
    RiverDepth.Init(0.f, Count);
    RiverPower.Init(0.f, Count);
    RiverBodyAt.Init(-1, Count);
    RiverDir.Init(FVector2f::ZeroVector, Count);
    TArray<int32> Source;
    Source.Init(-1, Count);
    for (int32 I = 0; I < Count; ++I)
    {
        MaxAccumulation = FMath::Max(MaxAccumulation, Accum[I]);
        if (Accum[I] >= RiverThreshold && Base[I] >= 0.f && LakeBody[I] < FirstLakeBody && Down[I] >= 0)
        {
            Source[I] = I;
            RiverDist[I] = 0.f;
            ++RiverNodes;
        }
    }
    auto Relax = [&](int32 I, int32 N)
    {
        if (Source[N] < 0)
        {
            return;
        }
        const int32 S = Source[N];
        const float D = static_cast<float>(FVector2D::Distance(NodePosition(FIntPoint(I % Nodes, I / Nodes)), NodePosition(FIntPoint(S % Nodes, S / Nodes))));
        if (D < RiverDist[I])
        {
            RiverDist[I] = D;
            Source[I] = S;
        }
    };
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        for (int32 Y = 0; Y < Nodes; ++Y)
        {
            for (int32 X = 0; X < Nodes; ++X)
            {
                const int32 I = Y * Nodes + X;
                if (X > 0) Relax(I, I - 1);
                if (Y > 0) Relax(I, I - Nodes);
                if (X > 0 && Y > 0) Relax(I, I - Nodes - 1);
                if (X < Nodes - 1 && Y > 0) Relax(I, I - Nodes + 1);
            }
        }
        for (int32 Y = Nodes - 1; Y >= 0; --Y)
        {
            for (int32 X = Nodes - 1; X >= 0; --X)
            {
                const int32 I = Y * Nodes + X;
                if (X < Nodes - 1) Relax(I, I + 1);
                if (Y < Nodes - 1) Relax(I, I + Nodes);
                if (X < Nodes - 1 && Y < Nodes - 1) Relax(I, I + Nodes + 1);
                if (X > 0 && Y < Nodes - 1) Relax(I, I + Nodes - 1);
            }
        }
    }
    for (int32 I = 0; I < Count; ++I)
    {
        const int32 S = Source[I];
        if (S < 0)
        {
            continue;
        }
        const float Root = FMath::Sqrt(Accum[S]);
        RiverLevel[I] = Filled[S];
        // Hydraulic geometry: width and depth grow with the square root of the drained area.
        RiverWidth[I] = FMath::Clamp(1.2f + .19f * Root, 3.f, 48.f);
        RiverDepth[I] = FMath::Clamp(.3f + .022f * Root, .4f, 4.5f);
        RiverPower[I] = FMath::Clamp((Root - FMath::Sqrt(RiverThreshold)) / 60.f, 0.f, 1.f);
        RiverBodyAt[I] = Accum[S] >= MainThreshold ? RiverBody : StreamBody;
        const int32 D = Down[S];
        if (D >= 0)
        {
            const FVector2D Dir = (NodePosition(FIntPoint(D % Nodes, D / Nodes)) - NodePosition(FIntPoint(S % Nodes, S / Nodes))).GetSafeNormal();
            const float Speed = Bodies[RiverBodyAt[I]].Props.CurrentSpeed;
            RiverDir[I] = FVector2f(Dir) * Speed;
        }
    }

    // 5. Waterfalls: a river or stream node that drops at least 4 m to the next one.
    for (int32 I = 0; I < Count; ++I)
    {
        const int32 D = Down[I];
        if (Source[I] != I || D < 0 || LakeBody[I] >= FirstLakeBody)
        {
            continue;
        }
        const float Drop = Filled[I] - Filled[D];
        if (Drop >= 4.f)
        {
            FWaterfallSite Site;
            Site.Position = NodePosition(FIntPoint(I % Nodes, I / Nodes));
            Site.Direction = (NodePosition(FIntPoint(D % Nodes, D / Nodes)) - Site.Position).GetSafeNormal();
            Site.Top = Filled[I];
            Site.Bottom = Filled[D];
            Site.Width = RiverWidth[I];
            Site.Body = WaterfallBody;
            Waterfalls.Add(Site);
        }
    }
    // The most striking falls near the spawn first: drop times (capped) width, within 2.5 km.
    auto Score = [](const FWaterfallSite& W)
    {
        return (W.Position.Size() < 2500.0 ? 1000.f : 0.f) + (W.Top - W.Bottom) * FMath::Min(W.Width, 12.f);
    };
    Waterfalls.Sort([&Score](const FWaterfallSite& A, const FWaterfallSite& B) { return Score(A) > Score(B); });
    if (Waterfalls.Num() > 24)
    {
        Waterfalls.SetNum(24);
    }
    // The river is drawn through the domain warp: move each fall to where its two nodes appear.
    for (FWaterfallSite& Site : Waterfalls)
    {
        const FVector2D Top = WorldOfGridPoint(Site.Position);
        const FVector2D Foot = WorldOfGridPoint(Site.Position + Site.Direction * NodeSpacing * (FMath::Abs(Site.Direction.X) > .5 && FMath::Abs(Site.Direction.Y) > .5 ? UE_SQRT_2 : 1.0));
        Site.Position = Top;
        if (!(Foot - Top).IsNearlyZero())
        {
            Site.Direction = (Foot - Top).GetSafeNormal();
        }
        Site.Length = FMath::Max(static_cast<float>(FVector2D::Distance(Top, Foot)), 4.f);
    }

    // 6. The main river: from the strongest outlet node, upstream along the largest tributary.
    int32 Mouth = -1;
    for (int32 I = 0; I < Count; ++I)
    {
        if (Source[I] == I && (Mouth < 0 || Accum[I] > Accum[Mouth]))
        {
            Mouth = I;
        }
    }
    TMap<int32, int32> Upstream; // node -> its largest tributary
    for (int32 I = 0; I < Count; ++I)
    {
        if (Source[I] == I && Down[I] >= 0)
        {
            int32& Best = Upstream.FindOrAdd(Down[I], -1);
            if (Best < 0 || Accum[I] > Accum[Best])
            {
                Best = I;
            }
        }
    }
    for (int32 N = Mouth, Guard = 0; N >= 0 && Guard < Count; ++Guard)
    {
        MainRiver.Insert(FIntPoint(N % Nodes, N / Nodes), 0);
        const int32* Up = Upstream.Find(N);
        N = Up ? *Up : -1;
    }
}

FVector2D FAethraWorldGen::RiverLookup(FVector2D P) const
{
    return P + FVector2D(Noise(P / 85.0 + Offsets[9] * .31), Noise(P / 85.0 + Offsets[10] * .29)) * 16.0;
}

FVector2D FAethraWorldGen::WorldOfGridPoint(FVector2D R) const
{
    // The warp has no closed-form inverse: search a 1 m grid within its amplitude for the best match.
    FVector2D Best = R;
    double BestError = TNumericLimits<double>::Max();
    for (int32 Y = -18; Y <= 18; ++Y)
    {
        for (int32 X = -18; X <= 18; ++X)
        {
            const FVector2D P = R + FVector2D(X, Y);
            const double Error = FVector2D::DistSquared(RiverLookup(P), R);
            if (Error < BestError)
            {
                BestError = Error;
                Best = P;
            }
        }
    }
    return Best;
}

int32 FAethraWorldGen::NearestNode(FVector2D P) const
{
    const int32 X = FMath::RoundToInt((P.X + DomainHalf) / NodeSpacing);
    const int32 Y = FMath::RoundToInt((P.Y + DomainHalf) / NodeSpacing);
    return X >= 0 && Y >= 0 && X < Nodes && Y < Nodes ? Y * Nodes + X : -1;
}

float FAethraWorldGen::Bilinear(const TArray<float>& Field, FVector2D P) const
{
    const double GX = (P.X + DomainHalf) / NodeSpacing, GY = (P.Y + DomainHalf) / NodeSpacing;
    const int32 X0 = FMath::Clamp(FMath::FloorToInt(GX), 0, Nodes - 2), Y0 = FMath::Clamp(FMath::FloorToInt(GY), 0, Nodes - 2);
    const float FX = FMath::Clamp(static_cast<float>(GX - X0), 0.f, 1.f), FY = FMath::Clamp(static_cast<float>(GY - Y0), 0.f, 1.f);
    const int32 I = Y0 * Nodes + X0;
    return FMath::Lerp(FMath::Lerp(Field[I], Field[I + 1], FX), FMath::Lerp(Field[I + Nodes], Field[I + Nodes + 1], FX), FY);
}

FWorldSample FAethraWorldGen::Sample(FVector2D P) const
{
    FWorldSample S;
    float H = BaseHeight(P);
    // Settlement pads: flat ground with a soft rim.
    for (const FSettlementPad& Pad : Pads)
    {
        const float D = static_cast<float>(FVector2D::Distance(P, Pad.Center));
        const float W = 1.f - Smooth(Pad.Radius, Pad.Radius + 14.f, D);
        H = FMath::Lerp(H, Pad.Height, W);
    }

    const bool bInDomain = FMath::Abs(P.X) < DomainHalf - NodeSpacing && FMath::Abs(P.Y) < DomainHalf - NodeSpacing;
    float WaterDist = 1e6f;
    if (bInDomain)
    {
        // Rivers are looked up through a smooth domain warp: the drainage grid gives the course, the
        // warp gives meanders a few metres to tens of metres wide instead of grid-straight channels.
        const FVector2D R = RiverLookup(P);
        const int32 Near = NearestNode(R);
        const float D = Bilinear(RiverDist, R);
        if (Near >= 0 && RiverBodyAt[Near] >= 0)
        {
            // The surface stays below the land it flows through (the drainage level can sit metres above
            // the local ground, which made the water stand above its own banks).
            const float Level = FMath::Min(Bilinear(RiverLevel, R), H - .8f);
            const float Half = .5f * Bilinear(RiverWidth, R);
            const float Depth = Bilinear(RiverDepth, R);
            // Carve: a parabolic bed inside the channel; a short steep bank, then a gentle rise.
            const float Out = D - Half;
            const float Limit = D < Half ? Level - Depth * (1.f - FMath::Square(D / Half)) - .15f
                : Level + .15f + FMath::Min(Out, 2.f) * .6f + FMath::Max(Out - 2.f, 0.f) * .3f;
            if (Limit < H)
            {
                H = Limit;
            }
            if (D < Half + 3.f)
            {
                S.ShoreLevel = Level;
            }
            if (D < Half + .5f)
            {
                S.WaterLevel = Level;
                S.WaterBody = RiverBodyAt[Near];
                S.Flow = RiverDir[Near];
            }
            S.RiverStrength = D < Half + 8.f ? RiverPower[Near] + .05f : 0.f;
            WaterDist = FMath::Max(0.f, D - Half);
        }
        // Lakes: the shore is the 0.5 contour of the interpolated lake mask (round, following the
        // ground), not the nearest node's 16 m square; the bed deepens toward the middle.
        const float Mask = Bilinear(LakeMask, R);
        if (Mask > .3f)
        {
            int32 Lake = -1;
            for (int32 K = 0; K < 4 && Lake < 0; ++K)
            {
                const int32 N = NearestNode(R + FVector2D((K & 1) ? NodeSpacing * .5 : -NodeSpacing * .5, (K & 2) ? NodeSpacing * .5 : -NodeSpacing * .5));
                Lake = N >= 0 && LakeBody[N] >= FirstLakeBody ? N : -1;
            }
            if (Lake >= 0)
            {
                S.ShoreLevel = Filled[Lake];
            }
            if (Lake >= 0 && Mask > .5f)
            {
                S.WaterLevel = Filled[Lake];
                S.WaterBody = LakeBody[Lake];
                H = FMath::Min(H, Filled[Lake] - 3.f * (Mask - .5f));
                WaterDist = 0.f;
            }
        }
    }
    if (H < 2.f && S.ShoreLevel < -1e8f && (S.WaterBody < 0 || S.WaterBody == SeaBody || !bInDomain))
    {
        S.ShoreLevel = 0.f;
    }
    if (H < .3f && (S.WaterBody < 0 || S.WaterBody == SeaBody || !bInDomain))
    {
        S.WaterLevel = 0.f;
        S.WaterBody = SeaBody;
        WaterDist = 0.f;
    }
    S.Height = H;
    if (S.IsWater())
    {
        WaterDist = 0.f;
    }
    S.WaterDistance = WaterDist;

    // Climate. The lapse rate is stylised (4.5 C per 100 m) so peaks a few hundred metres up are snowy.
    S.Temperature = 16.f + 4.f * Fbm(P / 3000.0 + Offsets[9], 2) - .045f * FMath::Max(H, 0.f);
    S.Moisture = FMath::Clamp(.5f + .35f * Fbm(P / 900.0 + Offsets[10], 3) + .35f * FMath::Exp(-WaterDist / 80.f), 0.f, 1.f);
    const float RockField = Fbm(P / 1700.0 + Offsets[11], 2);
    S.Rock = H > 200.f ? (RockField > 0.f ? EAmeRock::Slate : EAmeRock::Granite)
        : RockField > .3f ? EAmeRock::Limestone : RockField < -.35f ? EAmeRock::Sandstone : EAmeRock::Granite;

    const float Forest = FMath::Clamp((Fbm(P / 650.0 + Offsets[2] * .5, 3) + .1f + (S.Moisture - .5f) * .6f) * 1.6f, 0.f, 1.f);
    S.ForestDensity = Forest;
    if (S.WaterBody == SeaBody && H < 2.5f)
    {
        S.Biome = EAmeBiome::Beach;
    }
    else if (S.Temperature < -1.f)
    {
        S.Biome = EAmeBiome::SnowPeak;
    }
    else if (H > 160.f)
    {
        S.Biome = EAmeBiome::Mountain;
    }
    else if (S.RiverStrength > 0.f && WaterDist < 7.f)
    {
        S.Biome = EAmeBiome::RiverBank;
    }
    else if (S.Moisture > .84f && H < 60.f && WaterDist < 60.f)
    {
        S.Biome = EAmeBiome::Wetland;
    }
    else if (Forest > .45f)
    {
        S.Biome = EAmeBiome::Forest;
    }
    else
    {
        S.Biome = S.Moisture > .58f ? EAmeBiome::Meadow : EAmeBiome::Plains;
    }
    return S;
}
