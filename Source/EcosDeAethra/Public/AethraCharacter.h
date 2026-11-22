#pragma once

#include "AethraInventory.h"
#include "AethraVoxelTypes.h"
#include "GameFramework/Character.h"
#include "AethraCharacter.generated.h"

class UCameraComponent;

UCLASS()
class ECOSDEAETHRA_API AAethraCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AAethraCharacter();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

    EAethraBlock GetSelectedBlock() const { return Inventory.SelectedBlock(); }
    const FAethraInventory& GetInventory() const { return Inventory; }
    bool HasInventory() const { return InventorySeed != 0; }
    bool IsSwimming() const { return bSwimming; }
    bool HasRecentEditFailure() const;

private:
    UPROPERTY(VisibleAnywhere, Category="Camera")
    TObjectPtr<UCameraComponent> Camera;

    // Voxel worlds: the hotbar, loaded for the world's seed (0 = not loaded yet).
    FAethraInventory Inventory;
    int32 InventorySeed = 0;
    double ShowEditFailureUntil = -1.0;
    bool bSwimming = false;
    bool bSwimUp = false;

    // Benchmark route (-AethraBench=<seconds>): fixed inputs, CSV capture, then quit.
    float BenchSeconds = 0.0f;
    float BenchElapsed = 0.0f;
    float NextBenchEdit = 0.0f;
    bool bBenchCapturing = false;

    void MoveForward(float Value);
    void MoveRight(float Value);
    void RemoveBlock();
    void PlaceBlock();
    void SelectSlot(int32 Slot);
    void JumpPressed();
    void JumpReleased();
    void ToggleMenu();
    bool IsMenuOpen() const;
    void UpdateSwimming(class AAethraVoxelWorld& Terrain);
    void SaveInventory() const;
    void ApplyQuality(int32 Level);
    void EditBlock(bool bPlace);
};
