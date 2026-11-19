#include "AethraCharacter.h"

#include "AethraGameMode.h"
#include "AethraVoxelWorld.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Voxel/AethraTerrain.h"
#include "Voxel/AethraVoxelChunk.h"

AAethraCharacter::AAethraCharacter()
{
    GetCapsuleComponent()->InitCapsuleSize(34.0f, 90.0f);
    GetCharacterMovement()->MaxWalkSpeed = 520.0f;
    GetCharacterMovement()->JumpZVelocity = 500.0f;
    GetCharacterMovement()->AirControl = 0.25f;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0.0, 0.0, 65.0));
    Camera->bUsePawnControlRotation = true;
    bUseControllerRotationYaw = true;
}

void AAethraCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("MoveForward"), this, &AAethraCharacter::MoveForward);
    Input->BindAxis(TEXT("MoveRight"), this, &AAethraCharacter::MoveRight);
    Input->BindAxis(TEXT("LookYaw"), this, &AAethraCharacter::AddControllerYawInput);
    Input->BindAxis(TEXT("LookPitch"), this, &AAethraCharacter::AddControllerPitchInput);
    Input->BindAction(TEXT("Jump"), IE_Pressed, this, &AAethraCharacter::Jump);
    Input->BindAction(TEXT("Jump"), IE_Released, this, &AAethraCharacter::StopJumping);
    Input->BindAction(TEXT("RemoveBlock"), IE_Pressed, this, &AAethraCharacter::RemoveBlock);
    Input->BindAction(TEXT("PlaceBlock"), IE_Pressed, this, &AAethraCharacter::PlaceBlock);

    const auto BindBlock = [this, Input](const TCHAR* Action, EAethraBlock Block)
    {
        FInputActionBinding Binding(FName(Action), IE_Pressed);
        Binding.ActionDelegate.GetDelegateForManualSet().BindLambda([this, Block] { SelectBlock(Block); });
        Input->AddActionBinding(Binding);
    };
    BindBlock(TEXT("SelectGrass"), EAethraBlock::Grass);
    BindBlock(TEXT("SelectDirt"), EAethraBlock::Dirt);
    BindBlock(TEXT("SelectStone"), EAethraBlock::Stone);
    BindBlock(TEXT("SelectSand"), EAethraBlock::Sand);

    const auto BindQuality = [this, Input](const TCHAR* Action, int32 Level)
    {
        FInputActionBinding Binding(FName(Action), IE_Pressed);
        Binding.ActionDelegate.GetDelegateForManualSet().BindLambda([this, Level] { ApplyQuality(Level); });
        Input->AddActionBinding(Binding);
    };
    BindQuality(TEXT("QualityLow"), 0);
    BindQuality(TEXT("QualityMedium"), 1);
    BindQuality(TEXT("QualityHigh"), 2);
}

bool AAethraCharacter::HasRecentEditFailure() const
{
    return GetWorld()->GetTimeSeconds() < ShowEditFailureUntil;
}

void AAethraCharacter::MoveForward(float Value)
{
    AddMovementInput(GetActorForwardVector(), Value);
}

void AAethraCharacter::MoveRight(float Value)
{
    AddMovementInput(GetActorRightVector(), Value);
}

void AAethraCharacter::RemoveBlock()
{
    EditBlock(false);
}

void AAethraCharacter::PlaceBlock()
{
    EditBlock(true);
}

void AAethraCharacter::SelectBlock(EAethraBlock Block)
{
    SelectedBlock = Block;
}

void AAethraCharacter::ApplyQuality(int32 Level)
{
    const AAethraGameMode* Mode = GetWorld()->GetAuthGameMode<AAethraGameMode>();
    if (Mode && Mode->GetVoxelWorld())
    {
        Mode->GetVoxelWorld()->SetStreamingRadius(2 + Level);
    }
    if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
    {
        Settings->SetViewDistanceQuality(Level + 1);
        Settings->SetShadowQuality(Level + 1);
        Settings->SetAntiAliasingQuality(Level + 1);
        Settings->ApplyNonResolutionSettings();
    }
}

void AAethraCharacter::EditBlock(bool bPlace)
{
    const AAethraGameMode* Mode = GetWorld()->GetAuthGameMode<AAethraGameMode>();
    AAethraVoxelWorld* Terrain = Mode ? Mode->GetVoxelWorld() : nullptr;
    if (!Terrain)
    {
        return;
    }

    const FVector Start = Camera->GetComponentLocation();
    const FVector End = Start + Camera->GetForwardVector() * 800.0;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(AethraBlockEdit), true, this);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query)
        || !Cast<AAethraVoxelChunk>(Hit.GetActor()))
    {
        return;
    }

    const FVector Normal = Hit.ImpactNormal;
    const FVector Inside = Hit.ImpactPoint - Normal;
    const FIntVector Target = AethraTerrain::CellAt(Inside);
    if (Terrain->GetBlock(Target) == EAethraBlock::Air)
    {
        return;
    }
    if (!bPlace)
    {
        if (!Terrain->SetBlock(Target, EAethraBlock::Air))
        {
            ShowEditFailureUntil = GetWorld()->GetTimeSeconds() + 1.5;
        }
        return;
    }

    FIntVector Face;
    const FVector AbsNormal = Normal.GetAbs();
    if (AbsNormal.X >= AbsNormal.Y && AbsNormal.X >= AbsNormal.Z)
    {
        Face = FIntVector(Normal.X >= 0 ? 1 : -1, 0, 0);
    }
    else if (AbsNormal.Y >= AbsNormal.Z)
    {
        Face = FIntVector(0, Normal.Y >= 0 ? 1 : -1, 0);
    }
    else
    {
        Face = FIntVector(0, 0, Normal.Z >= 0 ? 1 : -1);
    }
    const FIntVector Destination = Target + Face;
    if (Terrain->GetBlock(Destination) != EAethraBlock::Air)
    {
        return;
    }

    const FVector Min = FVector(Destination) * AethraTerrain::CellSizeCm;
    const FBox BlockBounds(Min + FVector(0.1), Min + FVector(AethraTerrain::CellSizeCm - 0.1));
    if (!GetCapsuleComponent()->Bounds.GetBox().Intersect(BlockBounds)
        && !Terrain->SetBlock(Destination, SelectedBlock))
    {
        ShowEditFailureUntil = GetWorld()->GetTimeSeconds() + 1.5;
    }
}
