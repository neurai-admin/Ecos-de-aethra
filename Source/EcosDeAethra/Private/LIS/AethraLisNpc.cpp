#include "AethraLisNpc.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/AethraTerrain.h"

namespace
{
// Part order in Parts: legs, torso, arms, head, hat.
enum EPart { LegL, LegR, Torso, ArmL, ArmR, Head, Hat, PartCount };
constexpr float HalfHeight = 90.f;
}

AAethraLisNpc::AAethraLisNpc()
{
    PrimaryActorTick.bCanEverTick = false; // the director moves every NPC from one place

    Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
    Capsule->InitCapsuleSize(30.f, HalfHeight);
    // Blocks the player physically but not the block-edit and visibility traces.
    Capsule->SetCollisionProfileName(TEXT("Custom"));
    Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Capsule->SetCollisionObjectType(ECC_Pawn);
    Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
    Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    RootComponent = Capsule;

    Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
    Body->SetupAttachment(Capsule);
    Body->SetRelativeLocation(FVector(0, 0, -HalfHeight));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CubeMesh = Cube.Object;
    ShapeMaterial = Material.Object;

    // Blocky proportions in the voxel style: 1.8 m tall, made of boxes.
    AddPart(TEXT("LegL"), FVector(0, -11, 40), FVector(18, 18, 80));
    AddPart(TEXT("LegR"), FVector(0, 11, 40), FVector(18, 18, 80));
    AddPart(TEXT("Torso"), FVector(0, 0, 112), FVector(26, 44, 64));
    AddPart(TEXT("ArmL"), FVector(0, -30, 112), FVector(14, 14, 62));
    AddPart(TEXT("ArmR"), FVector(0, 30, 112), FVector(14, 14, 62));
    AddPart(TEXT("Head"), FVector(0, 0, 162), FVector(34, 34, 34));
    AddPart(TEXT("Hat"), FVector(0, 0, 184), FVector(40, 40, 10));
}

UStaticMeshComponent* AAethraLisNpc::AddPart(FName Name, FVector Location, FVector SizeCm)
{
    UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
    Part->SetupAttachment(Body);
    Part->SetStaticMesh(CubeMesh);
    Part->SetRelativeLocation(Location);
    Part->SetRelativeScale3D(SizeCm / 100.f); // the engine cube is 100 cm
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetCastShadow(true);
    Parts.Add(Part);
    return Part;
}

void AAethraLisNpc::SetLook(const FLinearColor& Clothes, const FLinearColor& Skin, const FLinearColor& Hat)
{
    const FLinearColor Trousers = Clothes * .45f;
    const FLinearColor Colors[PartCount] = { Trousers, Trousers, Clothes, Clothes, Clothes, Skin, Hat };
    for (int32 I = 0; I < Parts.Num() && I < PartCount; ++I)
    {
        if (UMaterialInstanceDynamic* Mid = Parts[I]->CreateDynamicMaterialInstance(0, ShapeMaterial))
        {
            Mid->SetVectorParameterValue(TEXT("Color"), Colors[I]);
        }
    }
}

FIntVector AAethraLisNpc::CurrentCell() const
{
    return AethraTerrain::CellAt(GetActorLocation() - FVector(0, 0, HalfHeight - 5.f));
}

void AAethraLisNpc::FollowPath(const TArray<FIntVector>& InPath, FIntVector InGoal)
{
    Path = InPath;
    PathIndex = 0;
    Goal = InGoal;
    StuckSeconds = 0.f;
    NavState = Path.Num() ? EAethraNavState::Walking : EAethraNavState::Arrived;
}

void AAethraLisNpc::Stop()
{
    Path.Reset();
    PathIndex = 0;
    NavState = EAethraNavState::Idle;
}

bool AAethraLisNpc::PathUses(FIntPoint Column) const
{
    for (int32 I = PathIndex; I < Path.Num(); ++I)
    {
        if (Path[I].X == Column.X && Path[I].Y == Column.Y)
        {
            return true;
        }
    }
    return false;
}

void AAethraLisNpc::FaceTowards(const FVector& Point)
{
    const FVector Direction = (Point - GetActorLocation()).GetSafeNormal2D();
    if (!Direction.IsNearlyZero())
    {
        SetActorRotation(Direction.Rotation());
    }
}

bool AAethraLisNpc::StepMovement(float DeltaSeconds)
{
    if (NavState != EAethraNavState::Walking)
    {
        return false;
    }
    if (!Path.IsValidIndex(PathIndex))
    {
        NavState = EAethraNavState::Arrived;
        return true;
    }
    // Kinematic walk from cell centre to cell centre; height follows the terrain, so it does not
    // depend on chunk collision being streamed in around the player.
    const FIntVector Cell = Path[PathIndex];
    const FVector Target((Cell.X + .5) * AethraTerrain::CellSizeCm, (Cell.Y + .5) * AethraTerrain::CellSizeCm,
        Cell.Z * AethraTerrain::CellSizeCm + HalfHeight);
    FVector Location = GetActorLocation();
    const FVector Flat(Target.X - Location.X, Target.Y - Location.Y, 0.0);
    const double Distance = Flat.Size();
    const double Step = WalkSpeed * DeltaSeconds;
    if (Distance <= Step)
    {
        Location.X = Target.X;
        Location.Y = Target.Y;
        ++PathIndex;
    }
    else
    {
        Location += Flat / Distance * Step;
        FaceTowards(Target);
    }
    // Step up quickly (a hop), step down a little slower.
    const double Rise = Target.Z - Location.Z;
    Location.Z += FMath::Clamp(Rise, -500.0 * DeltaSeconds, 700.0 * DeltaSeconds);
    SetActorLocation(Location);
    WalkPhase += DeltaSeconds * 9.f;
    if (PathIndex >= Path.Num())
    {
        NavState = EAethraNavState::Arrived;
        return true;
    }
    return false;
}

void AAethraLisNpc::SetLying(bool bInLying)
{
    if (bLying != bInLying)
    {
        bLying = bInLying;
        Body->SetRelativeRotation(bLying ? FRotator(-90.f, 0.f, 0.f) : FRotator::ZeroRotator);
        Body->SetRelativeLocation(bLying ? FVector(-90.f, 0.f, -HalfHeight + 15.f) : FVector(0, 0, -HalfHeight));
    }
}

void AAethraLisNpc::Animate(float DeltaSeconds, bool bDetailed)
{
    // Distant NPCs keep their pose: no per-part updates.
    if (!bDetailed || Parts.Num() < PartCount)
    {
        return;
    }
    const bool bMoving = NavState == EAethraNavState::Walking;
    if (!bMoving && !bPosed)
    {
        return; // already standing still: nothing to update
    }
    bPosed = bMoving;
    const float Swing = bMoving ? 32.f * FMath::Sin(WalkPhase) : 0.f;
    Parts[LegL]->SetRelativeRotation(FRotator(Swing, 0, 0));
    Parts[LegR]->SetRelativeRotation(FRotator(-Swing, 0, 0));
    Parts[ArmL]->SetRelativeRotation(FRotator(-Swing * .8f, 0, 0));
    Parts[ArmR]->SetRelativeRotation(FRotator(Swing * .8f, 0, 0));
}
