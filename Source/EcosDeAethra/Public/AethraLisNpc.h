// 3D body of one LIS villager. It owns no intelligence: the LIS decides, the director plans the path
// and picks the animation that matches the LIS action; this actor walks the path and poses its
// skeleton. One reusable rigged body (SK_Villager, 18 bones) with per-villager colours, hat, hair
// and tools. Animation is procedural over the skeleton (no animation assets): each state is a pose
// function of time, and state changes cross-fade.
#pragma once

#include "GameFramework/Actor.h"
#include "AethraLisNpc.generated.h"

class UCapsuleComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPoseableMeshComponent;
class USkeletalMesh;
class UStaticMesh;
class UStaticMeshComponent;

UENUM()
enum class EAethraNavState : uint8
{
    Idle,       // standing at its spot
    Waiting,    // path requested, not computed yet (path budget per frame)
    Walking,
    Arrived,
    Failed      // no path; the LIS was told
};

// Body animation states. The director derives them from the LIS action (see AnimFor).
UENUM()
enum class EAethraVillagerAnim : uint8
{
    Idle, Walk, Run, Chop, Hoe, Hammer, Gather, Talk, Trade, Sit, Eat, Sleep, Fish, Inspect, Count
};

UENUM()
enum class EAethraVillagerTool : uint8 { None, Axe, Hoe, Hammer, Basket };

UENUM()
enum class EAethraVillagerHat : uint8 { None, Straw, Beanie, Work, Merchant, Helmet };

struct FAethraVillagerLook
{
    // Colour regions of M_Villager: skin, tunic, trousers, leather, hair, pupil, eye white, accent.
    FLinearColor Regions[8];
    EAethraVillagerHat Hat = EAethraVillagerHat::None;
    FLinearColor HatColors[2] = { FLinearColor::White, FLinearColor::Black };
    bool bLongHair = false;
    bool bSatchel = false;
    float Height = 1.f; // scale
    float Build = 1.f;  // width scale
};

UCLASS()
class ECOSDEAETHRA_API AAethraLisNpc : public AActor
{
    GENERATED_BODY()

public:
    AAethraLisNpc();
    virtual void BeginPlay() override;

    // Persistent LIS identity id (FLisIdentity::Id). The link between the body and the mind.
    UPROPERTY(VisibleAnywhere, Category="LIS")
    int32 NpcId = 0;

    void SetLook(const FAethraVillagerLook& Look);
    void SetTool(EAethraVillagerTool Tool);
    EAethraVillagerTool GetTool() const { return Tool; }
    EAethraVillagerHat GetHat() const { return HatKind; }

    // Movement along a voxel path (standing cells). Speed in cm/s.
    void FollowPath(const TArray<FIntVector>& Path, FIntVector Goal);
    void Stop();
    // Advances along the path; returns true when the goal was reached this step.
    bool StepMovement(float DeltaSeconds);
    void SetAnim(EAethraVillagerAnim Anim);
    EAethraVillagerAnim GetAnim() const { return Anim; }
    // Poses the skeleton. bDetailed = near the player; far away the pose updates four times a second.
    void Animate(float DeltaSeconds, bool bDetailed);
    void SetLying(bool bLying);
    bool IsLying() const { return bLying; }
    void FaceTowards(const FVector& Point);
    FIntVector CurrentCell() const;
    bool PathUses(FIntPoint Column) const;
    bool HasSkeleton() const { return Bones.Num() > 0; }
    // Component-space location of a bone now (tests check that animation really moves the skeleton).
    FVector BoneLocation(FName Bone) const;

    EAethraNavState NavState = EAethraNavState::Idle;
    FIntVector Goal = FIntVector::ZeroValue;
    TArray<FIntVector> Path;
    int32 PathIndex = 0;
    int32 PathReplans = 0;
    FString LastError;
    TArray<FString> FailedActions; // most recent last, at most 4
    float WalkSpeed = 260.f;
    float RunSpeed = 430.f;
    bool bRunning = false;
    float StuckSeconds = 0.f;
    // What the NPC is doing at its spot (wander radius in cells, 0 = stay).
    int32 WanderRadius = 0;
    float NextWander = 0.f;
    // Which LIS step the body is showing (action, decision count, place); a change starts a new activity.
    uint64 ShownKey = 0;
    FIntVector Spot = FIntVector::ZeroValue;        // where it stands for the current activity
    FIntVector ActionTarget = FIntVector::ZeroValue; // block it works on (chop, crop)
    bool bActionPending = false;
    float ActionClock = 0.f;  // seconds working at the spot (a strike lands after a few swings)
    uint8 Phase = 0;          // sub-step of a multi-stage activity (builder: storage, then bench)
    FString Activity;         // what the body shows, in words (debug panel)

    void NoteFailure(const FString& What);

private:
    UPROPERTY(VisibleAnywhere, Category="LIS")
    TObjectPtr<UCapsuleComponent> Capsule;

    UPROPERTY(VisibleAnywhere, Category="LIS")
    TObjectPtr<UPoseableMeshComponent> Mesh;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> HatPart;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> HairPart;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ToolPart;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> SatchelPart;

    // Hard references so the cooker packages the villager assets.
    UPROPERTY()
    TObjectPtr<USkeletalMesh> BodyAsset;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> VillagerMaterial;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMesh>> HatAssets; // index = EAethraVillagerHat - 1

    UPROPERTY()
    TArray<TObjectPtr<UStaticMesh>> ToolAssets; // index = EAethraVillagerTool - 1

    UPROPERTY()
    TObjectPtr<UStaticMesh> HairAsset;

    UPROPERTY()
    TObjectPtr<UStaticMesh> SatchelAsset;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> BodyColors;

    // Skeleton data for the procedural poses.
    struct FBone
    {
        int32 Index = INDEX_NONE;
        FTransform RefLocal;
        FQuat ParentRefRot = FQuat::Identity; // parent's rest rotation in component space
        FTransform RefComponent;
    };
    TArray<FBone> Bones; // by EBone (AethraLisNpc.cpp)
    FVector Fwd = FVector(0, 1, 0), Left = FVector(1, 0, 0), Up = FVector(0, 0, 1); // mesh component axes
    float SwingSign = 1.f;

    EAethraVillagerAnim Anim = EAethraVillagerAnim::Idle;
    EAethraVillagerAnim PrevAnim = EAethraVillagerAnim::Idle;
    EAethraVillagerTool Tool = EAethraVillagerTool::None;
    EAethraVillagerHat HatKind = EAethraVillagerHat::None;
    float Blend = 1.f;      // 0..1 cross-fade from PrevAnim
    float AnimTime = 0.f;
    float PrevTime = 0.f;
    float WalkPhase = 0.f;
    float SlowClock = 0.f;
    float DesiredYaw = 0.f;
    float Variation = 0.f;  // per villager, so idles are not in sync
    bool bLying = false;

    void InitSkeleton();
    void AttachToBone(UStaticMeshComponent* Part, FName Bone);
    struct FPose;
    void Evaluate(EAethraVillagerAnim Which, float Time, FPose& Out) const;
    void ApplyPose(const FPose& Pose);
    UStaticMeshComponent* MakePart(FName Name);
};
