#include "AethraLisNpc.h"

#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/AethraTerrain.h"

namespace
{
constexpr float HalfHeight = 90.f;

// Bones of SK_Villager (Tools/Blender/make_villager.py), in the order of FPose.
enum EBone : int32
{
    Root, Pelvis, Spine, Chest, Neck, Head,
    UpperArmL, LowerArmL, HandL, ThighL, CalfL, FootL,
    UpperArmR, LowerArmR, HandR, ThighR, CalfR, FootR,
    BoneCount
};
const TCHAR* const BoneNames[BoneCount] = {
    TEXT("root"), TEXT("pelvis"), TEXT("spine"), TEXT("chest"), TEXT("neck"), TEXT("head"),
    TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"),
    TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r") };

const TCHAR* const HatPaths[] = {
    TEXT("/Game/Characters/Villager/SM_HatStraw.SM_HatStraw"), TEXT("/Game/Characters/Villager/SM_HatBeanie.SM_HatBeanie"),
    TEXT("/Game/Characters/Villager/SM_HatWork.SM_HatWork"), TEXT("/Game/Characters/Villager/SM_HatMerchant.SM_HatMerchant"),
    TEXT("/Game/Characters/Villager/SM_HatHelmet.SM_HatHelmet") };
const TCHAR* const ToolPaths[] = {
    TEXT("/Game/Characters/Villager/SM_Axe.SM_Axe"), TEXT("/Game/Characters/Villager/SM_Hoe.SM_Hoe"),
    TEXT("/Game/Characters/Villager/SM_Hammer.SM_Hammer"), TEXT("/Game/Characters/Villager/SM_Basket.SM_Basket") };

float Ease(float X)
{
    X = FMath::Clamp(X, 0.f, 1.f);
    return X * X * (3.f - 2.f * X);
}

// Raise-strike-recover cycle of a tool: angle at phase C in [0, 1).
float Strike(float C, float Rest, float Top, float Hit, float Raise, float Fall)
{
    if (C < Raise)
    {
        return FMath::Lerp(Rest, Top, Ease(C / Raise));
    }
    if (C < Raise + Fall)
    {
        const float K = (C - Raise) / Fall;
        return FMath::Lerp(Top, Hit, K * K);
    }
    return FMath::Lerp(Hit, Rest, Ease((C - Raise - Fall) / (1.f - Raise - Fall)));
}
}

struct AAethraLisNpc::FPose
{
    FQuat Rot[BoneCount];
    FVector PelvisOffset = FVector::ZeroVector;

    FPose()
    {
        for (FQuat& Q : Rot)
        {
            Q = FQuat::Identity;
        }
    }
};

AAethraLisNpc::AAethraLisNpc()
{
    PrimaryActorTick.bCanEverTick = false; // the director moves and animates every villager from one place

    Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
    Capsule->InitCapsuleSize(30.f, HalfHeight);
    // Blocks the player physically but not the block-edit and visibility traces.
    Capsule->SetCollisionProfileName(TEXT("Custom"));
    Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Capsule->SetCollisionObjectType(ECC_Pawn);
    Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
    Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    RootComponent = Capsule;

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Body(TEXT("/Game/Characters/Villager/SK_Villager.SK_Villager"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Characters/Villager/M_Villager.M_Villager"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Hair(TEXT("/Game/Characters/Villager/SM_HairLong.SM_HairLong"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Satchel(TEXT("/Game/Characters/Villager/SM_Satchel.SM_Satchel"));
    BodyAsset = Body.Object;
    VillagerMaterial = Material.Object;
    HairAsset = Hair.Object;
    SatchelAsset = Satchel.Object;
    for (const TCHAR* AssetPath : HatPaths)
    {
        HatAssets.Add(ConstructorHelpers::FObjectFinder<UStaticMesh>(AssetPath).Object);
    }
    for (const TCHAR* AssetPath : ToolPaths)
    {
        ToolAssets.Add(ConstructorHelpers::FObjectFinder<UStaticMesh>(AssetPath).Object);
    }

    Mesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Body"));
    Mesh->SetupAttachment(Capsule);
    Mesh->SetRelativeLocation(FVector(0, 0, -HalfHeight));
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(true);
    if (BodyAsset)
    {
        Mesh->SetSkinnedAssetAndUpdate(BodyAsset);
    }
    HatPart = MakePart(TEXT("Hat"));
    HairPart = MakePart(TEXT("Hair"));
    ToolPart = MakePart(TEXT("Tool"));
    SatchelPart = MakePart(TEXT("Satchel"));
}

UStaticMeshComponent* AAethraLisNpc::MakePart(FName Name)
{
    UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
    Part->SetupAttachment(Mesh);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetCastShadow(true);
    Part->SetVisibility(false);
    return Part;
}

void AAethraLisNpc::BeginPlay()
{
    Super::BeginPlay();
    InitSkeleton();
}

void AAethraLisNpc::InitSkeleton()
{
    if (Bones.Num() || !BodyAsset || !Mesh || !Mesh->IsRegistered())
    {
        return;
    }
    if (Mesh->BoneSpaceTransforms.Num() == 0)
    {
        Mesh->RefreshBoneTransforms();
    }
    const FReferenceSkeleton& Ref = BodyAsset->GetRefSkeleton();
    const TArray<FTransform>& Pose = Ref.GetRefBonePose();
    if (Mesh->BoneSpaceTransforms.Num() != Ref.GetNum())
    {
        return;
    }
    TArray<FTransform> Component;
    Component.SetNum(Ref.GetNum());
    for (int32 I = 0; I < Ref.GetNum(); ++I)
    {
        const int32 Parent = Ref.GetParentIndex(I);
        Component[I] = Parent == INDEX_NONE ? Pose[I] : Pose[I] * Component[Parent];
    }
    Bones.SetNum(BoneCount);
    for (int32 B = 0; B < BoneCount; ++B)
    {
        FBone& Bone = Bones[B];
        Bone.Index = Ref.FindBoneIndex(BoneNames[B]);
        if (Bone.Index == INDEX_NONE)
        {
            Bones.Reset(); // not the villager skeleton: keep the rest pose
            return;
        }
        const int32 Parent = Ref.GetParentIndex(Bone.Index);
        Bone.RefLocal = Pose[Bone.Index];
        Bone.RefComponent = Component[Bone.Index];
        Bone.ParentRefRot = Parent == INDEX_NONE ? FQuat::Identity : Component[Parent].GetRotation();
    }
    // Axes of the imported mesh, from the skeleton itself: left from the shoulders, forward from
    // left x up (FBX import mirrors one axis, so this holds whatever way the asset was turned).
    Up = FVector::UpVector;
    Left = (Bones[UpperArmL].RefComponent.GetLocation() - Bones[UpperArmR].RefComponent.GetLocation()).GetSafeNormal2D();
    Fwd = FVector::CrossProduct(Up, Left).GetSafeNormal();
    SwingSign = FVector::DotProduct(FQuat(Left, .3f).RotateVector(-Up), Fwd) > 0.f ? 1.f : -1.f;
    // Turn the mesh so its forward is the actor's forward (+X).
    Mesh->SetRelativeRotation(FRotator(0.f, -Fwd.Rotation().Yaw, 0.f));
    AttachToBone(HatPart, TEXT("head"));
    AttachToBone(HairPart, TEXT("head"));
    AttachToBone(ToolPart, TEXT("hand_r"));
    AttachToBone(SatchelPart, TEXT("pelvis"));
}

void AAethraLisNpc::AttachToBone(UStaticMeshComponent* Part, FName Bone)
{
    // Accessories are modelled in character space: undo the bone's rest transform so they sit where
    // they were modelled, then follow the bone.
    const int32 Index = BodyAsset ? BodyAsset->GetRefSkeleton().FindBoneIndex(Bone) : INDEX_NONE;
    for (const FBone& B : Bones)
    {
        if (B.Index == Index)
        {
            Part->AttachToComponent(Mesh, FAttachmentTransformRules::KeepRelativeTransform, Bone);
            Part->SetRelativeTransform(B.RefComponent.Inverse());
            return;
        }
    }
}

void AAethraLisNpc::SetLook(const FAethraVillagerLook& Look)
{
    InitSkeleton();
    if (!BodyColors && VillagerMaterial)
    {
        BodyColors = Mesh->CreateDynamicMaterialInstance(0, VillagerMaterial);
    }
    if (BodyColors)
    {
        for (int32 I = 0; I < 8; ++I)
        {
            BodyColors->SetVectorParameterValue(*FString::Printf(TEXT("Color%d"), I), Look.Regions[I]);
        }
    }
    auto Paint = [this](UStaticMeshComponent* Part, const FLinearColor& A, const FLinearColor& B)
    {
        if (UMaterialInstanceDynamic* Mid = Part->CreateDynamicMaterialInstance(0, VillagerMaterial))
        {
            Mid->SetVectorParameterValue(TEXT("Color0"), A);
            Mid->SetVectorParameterValue(TEXT("Color1"), B);
        }
    };
    HatKind = Look.Hat;
    const int32 HatIndex = static_cast<int32>(Look.Hat) - 1;
    HatPart->SetStaticMesh(HatAssets.IsValidIndex(HatIndex) ? HatAssets[HatIndex].Get() : nullptr);
    HatPart->SetVisibility(HatAssets.IsValidIndex(HatIndex));
    Paint(HatPart, Look.HatColors[0], Look.HatColors[1]);
    HairPart->SetStaticMesh(HairAsset);
    HairPart->SetVisibility(Look.bLongHair);
    Paint(HairPart, Look.Regions[4], Look.Regions[4]);
    SatchelPart->SetStaticMesh(SatchelAsset);
    SatchelPart->SetVisibility(Look.bSatchel);
    Paint(SatchelPart, FLinearColor(.28f, .14f, .06f), FLinearColor(.2f, .1f, .04f));
    Mesh->SetRelativeScale3D(FVector(Look.Build, Look.Build, Look.Height));
    Variation = (NpcId * 0.618f - FMath::FloorToFloat(NpcId * 0.618f)) * 6.2832f;
}

void AAethraLisNpc::SetTool(EAethraVillagerTool InTool)
{
    if (Tool == InTool)
    {
        return;
    }
    Tool = InTool;
    const int32 Index = static_cast<int32>(Tool) - 1;
    UStaticMesh* Asset = ToolAssets.IsValidIndex(Index) ? ToolAssets[Index].Get() : nullptr;
    ToolPart->SetStaticMesh(Asset);
    ToolPart->SetVisibility(Asset != nullptr);
    if (Asset && VillagerMaterial)
    {
        if (UMaterialInstanceDynamic* Mid = ToolPart->CreateDynamicMaterialInstance(0, VillagerMaterial))
        {
            Mid->SetVectorParameterValue(TEXT("Color0"), Tool == EAethraVillagerTool::Basket ? FLinearColor(.45f, .3f, .12f) : FLinearColor(.3f, .17f, .07f));
            Mid->SetVectorParameterValue(TEXT("Color1"), FLinearColor(.42f, .44f, .47f));
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
        DesiredYaw = Direction.Rotation().Yaw;
    }
}

void AAethraLisNpc::NoteFailure(const FString& What)
{
    LastError = What;
    FailedActions.Add(What);
    if (FailedActions.Num() > 4)
    {
        FailedActions.RemoveAt(0);
    }
}

bool AAethraLisNpc::StepMovement(float DeltaSeconds)
{
    // Turn smoothly toward where the villager wants to face (walking or working).
    const float Yaw = GetActorRotation().Yaw;
    const float Turn = FMath::FindDeltaAngleDegrees(Yaw, DesiredYaw);
    if (FMath::Abs(Turn) > .5f)
    {
        SetActorRotation(FRotator(0.f, Yaw + FMath::Clamp(Turn, -540.f * DeltaSeconds, 540.f * DeltaSeconds), 0.f));
    }
    if (NavState != EAethraNavState::Walking)
    {
        return false;
    }
    if (!Path.IsValidIndex(PathIndex))
    {
        NavState = EAethraNavState::Arrived;
        return true;
    }
    // Kinematic walk from cell centre to cell centre; height follows the path, so it does not
    // depend on chunk collision being streamed in around the player.
    const FIntVector Cell = Path[PathIndex];
    const FVector Target((Cell.X + .5) * AethraTerrain::CellSizeCm, (Cell.Y + .5) * AethraTerrain::CellSizeCm,
        Cell.Z * AethraTerrain::CellSizeCm + HalfHeight);
    FVector Location = GetActorLocation();
    const FVector Flat(Target.X - Location.X, Target.Y - Location.Y, 0.0);
    const double Distance = Flat.Size();
    if (Distance > 1.0)
    {
        DesiredYaw = Flat.Rotation().Yaw;
    }
    // A sharp corner: slow down while turning instead of sliding sideways.
    const float Facing = FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, DesiredYaw));
    const double Step = (bRunning ? RunSpeed : WalkSpeed) * DeltaSeconds * (Facing > 70.f ? .35 : 1.0);
    double Moved = Step;
    if (Distance <= Step)
    {
        Moved = Distance;
        Location.X = Target.X;
        Location.Y = Target.Y;
        ++PathIndex;
    }
    else
    {
        Location += Flat / Distance * Step;
    }
    // Step up quickly (a hop), step down a little slower.
    const double Rise = Target.Z - Location.Z;
    Location.Z += FMath::Clamp(Rise, -500.0 * DeltaSeconds, 700.0 * DeltaSeconds);
    SetActorLocation(Location);
    // One walk cycle (two steps) per 1.3 m, per 2.2 m running.
    WalkPhase = FMath::Fmod(WalkPhase + static_cast<float>(Moved) / (bRunning ? 220.f : 130.f) * 6.2832f, 6.2832f);
    if (PathIndex >= Path.Num())
    {
        NavState = EAethraNavState::Arrived;
        return true;
    }
    return false;
}

void AAethraLisNpc::SetLying(bool bInLying)
{
    if (bLying == bInLying)
    {
        return;
    }
    bLying = bInLying;
    InitSkeleton();
    const FQuat Base(FRotator(0.f, -Fwd.Rotation().Yaw, 0.f));
    Mesh->SetRelativeRotation(bLying ? FQuat(FRotator(-90.f, 0.f, 0.f)) * Base : Base);
    Mesh->SetRelativeLocation(bLying ? FVector(-85.f, 0.f, -HalfHeight + 14.f) : FVector(0, 0, -HalfHeight));
}

void AAethraLisNpc::SetAnim(EAethraVillagerAnim NewAnim)
{
    if (NewAnim == Anim)
    {
        return;
    }
    PrevAnim = Anim;
    PrevTime = AnimTime;
    Anim = NewAnim;
    AnimTime = 0.f;
    Blend = 0.f;
}

FVector AAethraLisNpc::BoneLocation(FName Bone) const
{
    return Mesh ? Mesh->GetBoneLocationByName(Bone, EBoneSpaces::ComponentSpace) : FVector::ZeroVector;
}

void AAethraLisNpc::Evaluate(EAethraVillagerAnim Which, float T, FPose& P) const
{
    auto Rad = [](float Deg) { return FMath::DegreesToRadians(Deg); };
    // Swing: + moves a hanging limb forward (and tips an upright bone back).
    auto Swing = [&](float Deg) { return FQuat(Left, Rad(Deg) * SwingSign); };
    auto Turn = [&](float Deg) { return FQuat(Up, Rad(Deg)); };
    // Out: + moves the limb away from the body on its own side.
    auto Out = [&](float Deg, bool bLeftSide)
    {
        const float Sign = FVector::DotProduct(FQuat(Fwd, .3f).RotateVector(-Up), Left) > 0.f ? 1.f : -1.f;
        return FQuat(Fwd, Rad(Deg) * Sign * (bLeftSide ? 1.f : -1.f));
    };
    const float V = Variation;
    const float Breath = FMath::Sin(T * 1.7f + V);

    switch (Which)
    {
    case EAethraVillagerAnim::Walk:
    case EAethraVillagerAnim::Run:
    {
        const bool bRun = Which == EAethraVillagerAnim::Run;
        const float W = WalkPhase, S = FMath::Sin(W), C = FMath::Cos(W);
        const float Leg = bRun ? 44.f : 26.f, Knee = bRun ? 75.f : 38.f, Arm = bRun ? 38.f : 22.f;
        P.Rot[ThighL] = Swing(Leg * S);
        P.Rot[ThighR] = Swing(-Leg * S);
        P.Rot[CalfL] = Swing(-Knee * FMath::Max(0.f, C));
        P.Rot[CalfR] = Swing(-Knee * FMath::Max(0.f, -C));
        P.Rot[UpperArmL] = Swing(-Arm * S) * Out(4.f, true);
        P.Rot[UpperArmR] = Swing(Arm * S) * Out(4.f, false);
        P.Rot[LowerArmL] = Swing(bRun ? 70.f : 14.f);
        P.Rot[LowerArmR] = Swing(bRun ? 70.f : 14.f);
        P.Rot[Spine] = Swing(bRun ? -10.f : -3.f);
        P.Rot[Chest] = Turn(5.f * S);
        P.Rot[Head] = Turn(-4.f * S);
        P.PelvisOffset = Up * ((bRun ? 4.f : 1.8f) * FMath::Abs(C) - (bRun ? 3.f : 1.f));
        break;
    }
    case EAethraVillagerAnim::Chop:
    {
        const float C = FMath::Frac(T / 1.25f);
        const float A = Strike(C, 25.f, 160.f, 30.f, .55f, .13f);
        const float Lean = C < .55f ? FMath::Lerp(-4.f, 6.f, Ease(C / .55f)) : C < .68f ? FMath::Lerp(6.f, -18.f, (C - .55f) / .13f) : FMath::Lerp(-18.f, -4.f, Ease((C - .68f) / .32f));
        P.Rot[UpperArmL] = Swing(A) * Out(-12.f, true);
        P.Rot[UpperArmR] = Swing(A) * Out(-12.f, false);
        P.Rot[LowerArmL] = Swing(15.f);
        P.Rot[LowerArmR] = Swing(15.f);
        P.Rot[Spine] = Swing(Lean);
        P.Rot[Head] = Swing(-.4f * Lean);
        P.Rot[ThighL] = Swing(10.f);
        P.Rot[ThighR] = Swing(-8.f);
        P.Rot[CalfL] = Swing(-6.f);
        break;
    }
    case EAethraVillagerAnim::Hoe:
    {
        const float C = FMath::Frac(T / 1.6f);
        const float A = Strike(C, 22.f, 110.f, 12.f, .5f, .15f);
        const float Lean = -10.f - 16.f * (C > .5f && C < .8f ? 1.f - FMath::Abs((C - .65f) / .15f) : 0.f);
        P.Rot[UpperArmL] = Swing(A) * Out(-10.f, true);
        P.Rot[UpperArmR] = Swing(A) * Out(-10.f, false);
        P.Rot[LowerArmL] = Swing(20.f);
        P.Rot[LowerArmR] = Swing(20.f);
        P.Rot[Spine] = Swing(Lean);
        P.Rot[ThighL] = Swing(14.f);
        P.Rot[ThighR] = Swing(-6.f);
        P.Rot[CalfL] = Swing(-14.f);
        P.Rot[CalfR] = Swing(-10.f);
        P.PelvisOffset = Up * -3.f;
        break;
    }
    case EAethraVillagerAnim::Hammer:
    {
        const float C = FMath::Frac(T / .7f);
        P.Rot[UpperArmR] = Swing(Strike(C, 50.f, 118.f, 42.f, .6f, .14f));
        P.Rot[LowerArmR] = Swing(25.f);
        P.Rot[UpperArmL] = Swing(48.f) * Out(-8.f, true);
        P.Rot[LowerArmL] = Swing(40.f);
        P.Rot[Spine] = Swing(-12.f);
        P.Rot[Head] = Swing(-14.f);
        break;
    }
    case EAethraVillagerAnim::Gather:
    {
        const float C = FMath::Frac(T / 2.4f);
        const float D = FMath::Square(FMath::Sin(PI * C));
        P.Rot[Spine] = Swing(-42.f * D);
        P.Rot[Chest] = Swing(-10.f * D);
        P.Rot[Head] = Swing(14.f * D);
        P.Rot[ThighL] = Swing(34.f * D);
        P.Rot[ThighR] = Swing(34.f * D);
        P.Rot[CalfL] = Swing(-56.f * D);
        P.Rot[CalfR] = Swing(-56.f * D);
        P.Rot[UpperArmL] = Swing(8.f + 58.f * D);
        P.Rot[UpperArmR] = Swing(8.f + 58.f * D);
        P.Rot[LowerArmL] = Swing(20.f * D);
        P.Rot[LowerArmR] = Swing(20.f * D);
        P.PelvisOffset = Up * (-15.f * D);
        break;
    }
    case EAethraVillagerAnim::Inspect:
        P.Rot[Spine] = Swing(-12.f);
        P.Rot[Head] = Swing(-16.f) * Turn(22.f * FMath::Sin(T * .8f + V));
        P.Rot[UpperArmR] = Swing(38.f + 4.f * Breath);
        P.Rot[LowerArmR] = Swing(78.f);
        P.Rot[UpperArmL] = Out(6.f, true);
        P.Rot[LowerArmL] = Swing(10.f);
        break;
    case EAethraVillagerAnim::Talk:
        P.Rot[UpperArmR] = Swing(24.f + 18.f * FMath::Sin(T * 2.4f + V)) * Out(6.f, false);
        P.Rot[LowerArmR] = Swing(48.f);
        P.Rot[UpperArmL] = Swing(10.f + 8.f * FMath::Sin(T * 1.7f)) * Out(5.f, true);
        P.Rot[LowerArmL] = Swing(16.f);
        P.Rot[Head] = Swing(3.f * FMath::Sin(T * 3.f)) * Turn(8.f * FMath::Sin(T * .7f + V));
        P.Rot[Chest] = Swing(1.f * Breath);
        break;
    case EAethraVillagerAnim::Trade:
        P.Rot[UpperArmL] = Swing(38.f + 8.f * FMath::Sin(T * 2.f)) * Out(-6.f, true);
        P.Rot[UpperArmR] = Swing(38.f + 8.f * FMath::Sin(T * 2.f + 1.5f)) * Out(-6.f, false);
        P.Rot[LowerArmL] = Swing(48.f);
        P.Rot[LowerArmR] = Swing(48.f);
        P.Rot[Head] = Turn(16.f * FMath::Sin(T * .9f + V));
        P.Rot[Chest] = Turn(6.f * FMath::Sin(T * .9f + V));
        break;
    case EAethraVillagerAnim::Sit:
    case EAethraVillagerAnim::Eat:
    {
        // Sitting on the ground, legs forward, hands on the knees.
        P.Rot[ThighL] = Swing(84.f) * Out(6.f, true);
        P.Rot[ThighR] = Swing(84.f) * Out(6.f, false);
        P.Rot[CalfL] = Swing(-8.f);
        P.Rot[CalfR] = Swing(-8.f);
        P.Rot[Spine] = Swing(6.f);
        P.Rot[Chest] = Swing(1.2f * Breath);
        P.Rot[UpperArmL] = Swing(32.f);
        P.Rot[UpperArmR] = Swing(32.f);
        P.Rot[LowerArmL] = Swing(18.f);
        P.Rot[LowerArmR] = Swing(18.f);
        P.Rot[Head] = Turn(10.f * FMath::Sin(T * .4f + V));
        P.PelvisOffset = Up * -60.f;
        if (Which == EAethraVillagerAnim::Eat)
        {
            const float E = .5f + .5f * FMath::Sin(T * 2.2f);
            P.Rot[UpperArmR] = Swing(50.f + 16.f * E);
            P.Rot[LowerArmR] = Swing(95.f + 25.f * E);
            P.Rot[Head] = Swing(-5.f * E);
        }
        break;
    }
    case EAethraVillagerAnim::Sleep:
        P.Rot[Chest] = Swing(2.f * FMath::Sin(T * 1.1f));
        P.Rot[UpperArmL] = Out(4.f, true);
        P.Rot[UpperArmR] = Out(4.f, false);
        P.Rot[Head] = Turn(12.f);
        break;
    case EAethraVillagerAnim::Fish:
    {
        const float Tug = FMath::Frac(T / 7.f + V) > .9f ? 14.f : 0.f;
        P.Rot[UpperArmL] = Swing(52.f + Tug) * Out(-10.f, true);
        P.Rot[UpperArmR] = Swing(52.f + Tug) * Out(-10.f, false);
        P.Rot[LowerArmL] = Swing(24.f);
        P.Rot[LowerArmR] = Swing(24.f);
        P.Rot[Spine] = Turn(5.f * FMath::Sin(T * .6f + V));
        P.Rot[Head] = Swing(-8.f);
        break;
    }
    default: // Idle: breathing, weight shift and a slow look around
        P.Rot[Chest] = Swing(1.2f * Breath);
        P.Rot[UpperArmL] = Out(5.f, true) * Swing(2.f * Breath);
        P.Rot[UpperArmR] = Out(5.f, false) * Swing(2.f * Breath);
        P.Rot[LowerArmL] = Swing(7.f);
        P.Rot[LowerArmR] = Swing(7.f);
        P.Rot[Head] = Turn(14.f * FMath::Sin(T * .37f + 3.f * V)) * Swing(-3.f * FMath::Sin(T * .23f + V));
        P.PelvisOffset = Left * (1.2f * FMath::Sin(T * .5f + V));
        break;
    }
}

void AAethraLisNpc::ApplyPose(const FPose& Pose)
{
    TArray<FTransform>& Space = Mesh->BoneSpaceTransforms;
    for (int32 B = 0; B < BoneCount; ++B)
    {
        const FBone& Bone = Bones[B];
        if (!Space.IsValidIndex(Bone.Index))
        {
            continue;
        }
        // The offset is a rotation about the joint in component axes; bring it into the parent's frame.
        const FQuat& Parent = Bone.ParentRefRot;
        FTransform Local = Bone.RefLocal;
        Local.SetRotation((Parent.Inverse() * Pose.Rot[B] * Parent * Bone.RefLocal.GetRotation()).GetNormalized());
        if (B == Pelvis)
        {
            Local.AddToTranslation(Parent.Inverse().RotateVector(Pose.PelvisOffset));
        }
        Space[Bone.Index] = Local;
    }
    Mesh->RefreshBoneTransforms();
}

void AAethraLisNpc::Animate(float DeltaSeconds, bool bDetailed)
{
    InitSkeleton();
    if (!HasSkeleton() || IsHidden())
    {
        return;
    }
    AnimTime += DeltaSeconds;
    PrevTime += DeltaSeconds;
    Blend = FMath::Min(1.f, Blend + DeltaSeconds / .3f);
    // Far away: a few pose updates per second are enough.
    SlowClock += DeltaSeconds;
    if (!bDetailed && SlowClock < .25f)
    {
        return;
    }
    SlowClock = 0.f;

    FPose Pose;
    Evaluate(Anim, AnimTime, Pose);
    if (Blend < 1.f)
    {
        FPose From;
        Evaluate(PrevAnim, PrevTime, From);
        const float K = Ease(Blend);
        for (int32 B = 0; B < BoneCount; ++B)
        {
            Pose.Rot[B] = FQuat::Slerp(From.Rot[B], Pose.Rot[B], K);
        }
        Pose.PelvisOffset = FMath::Lerp(From.PelvisOffset, Pose.PelvisOffset, K);
    }
    // Turning on the spot: a small shuffle of the feet.
    const float Turning = FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, DesiredYaw));
    if (Turning > 15.f && Anim != EAethraVillagerAnim::Walk && Anim != EAethraVillagerAnim::Run && !bLying)
    {
        const float Shuffle = 10.f * FMath::Sin(AnimTime * 14.f);
        Pose.Rot[ThighL] = FQuat(Left, FMath::DegreesToRadians(Shuffle) * SwingSign) * Pose.Rot[ThighL];
        Pose.Rot[ThighR] = FQuat(Left, FMath::DegreesToRadians(-Shuffle) * SwingSign) * Pose.Rot[ThighR];
    }
    ApplyPose(Pose);
}
