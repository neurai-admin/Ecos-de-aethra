#pragma once

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
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

    EAethraBlock GetSelectedBlock() const { return SelectedBlock; }
    bool HasRecentEditFailure() const;

private:
    UPROPERTY(VisibleAnywhere, Category="Camera")
    TObjectPtr<UCameraComponent> Camera;

    EAethraBlock SelectedBlock = EAethraBlock::Grass;
    double ShowEditFailureUntil = -1.0;

    void MoveForward(float Value);
    void MoveRight(float Value);
    void RemoveBlock();
    void PlaceBlock();
    void SelectBlock(EAethraBlock Block);
    void ApplyQuality(int32 Level);
    void EditBlock(bool bPlace);
};
