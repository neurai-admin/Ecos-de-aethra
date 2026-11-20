// Provisional 3D body of one LIS NPC. It owns no intelligence: the LIS decides, the director
// plans the path, and this actor only walks it and plays a simple block-style animation.
#pragma once

#include "GameFramework/Actor.h"
#include "AethraLisNpc.generated.h"

class UCapsuleComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

UENUM()
enum class EAethraNavState : uint8
{
    Idle,       // standing at its spot
    Waiting,    // path requested, not computed yet (path budget per frame)
    Walking,
    Arrived,
    Failed      // no path; the LIS was told
};

UCLASS()
class ECOSDEAETHRA_API AAethraLisNpc : public AActor
{
    GENERATED_BODY()

public:
    AAethraLisNpc();

    // Persistent LIS identity id (FLisIdentity::Id). The link between the body and the mind.
    UPROPERTY(VisibleAnywhere, Category="LIS")
    int32 NpcId = 0;

    void SetLook(const FLinearColor& Clothes, const FLinearColor& Skin, const FLinearColor& Hat);

    // Movement along a voxel path (standing cells). Speed in cm/s.
    void FollowPath(const TArray<FIntVector>& Path, FIntVector Goal);
    void Stop();
    // Advances along the path; returns true when the goal was reached this step.
    bool StepMovement(float DeltaSeconds);
    void Animate(float DeltaSeconds, bool bDetailed);
    void SetLying(bool bLying);
    void FaceTowards(const FVector& Point);
    FIntVector CurrentCell() const;
    bool PathUses(FIntPoint Column) const;

    EAethraNavState NavState = EAethraNavState::Idle;
    FIntVector Goal = FIntVector::ZeroValue;
    TArray<FIntVector> Path;
    int32 PathIndex = 0;
    int32 PathReplans = 0;
    FString LastError;
    float WalkSpeed = 260.f;
    float StuckSeconds = 0.f;
    // What the NPC is doing at its spot (wander radius in cells, 0 = stay).
    int32 WanderRadius = 0;
    float NextWander = 0.f;
    // Which LIS step the body is showing (action, decision count, place); a change starts a new activity.
    uint64 ShownKey = 0;
    FIntVector Spot = FIntVector::ZeroValue;        // where it stands for the current activity
    FIntVector ActionTarget = FIntVector::ZeroValue; // block it works on (chop)
    bool bActionPending = false;

private:
    UPROPERTY(VisibleAnywhere, Category="LIS")
    TObjectPtr<UCapsuleComponent> Capsule;

    UPROPERTY()
    TObjectPtr<USceneComponent> Body;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Parts;

    // Hard references so the cooker packages the engine shapes used by the placeholder body.
    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> ShapeMaterial;

    float WalkPhase = 0.f;
    bool bLying = false;
    bool bPosed = false; // limbs are away from the rest pose
    UStaticMeshComponent* AddPart(FName Name, FVector Location, FVector SizeCm);
};
