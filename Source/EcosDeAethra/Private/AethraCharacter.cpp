#include "AethraCharacter.h"

#include "AethraGameMode.h"
#include "AethraHUD.h"
#include "GameFramework/PlayerController.h"
#include "AethraVoxelWorld.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Voxel/AethraTerrain.h"
#include "Voxel/AethraVoxelChunk.h"
#include "VisualWorld/AethraVisualWorld.h"

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

void AAethraCharacter::BeginPlay()
{
    Super::BeginPlay();
    FParse::Value(FCommandLine::Get(), TEXT("AethraBench="), BenchSeconds);
    SetActorTickEnabled(true); // swimming and the inventory need it too
}

void AAethraCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    APlayerController* PC = Cast<APlayerController>(GetController());
    const AAethraGameMode* Mode = GetWorld()->GetAuthGameMode<AAethraGameMode>();
    if (AAethraVoxelWorld* Terrain = Mode ? Mode->GetVoxelWorld() : nullptr)
    {
        if (InventorySeed != Terrain->Seed)
        {
            InventorySeed = Terrain->Seed;
            Inventory = FAethraInventory();
            Inventory.Load(FAethraInventory::PathFor(InventorySeed)); // a new world starts empty
        }
        if (BenchSeconds <= 0.0f)
        {
            UpdateSwimming(*Terrain);
        }
    }
    if (BenchSeconds <= 0.0f || !PC)
    {
        return;
    }

    // Same seed + same inputs = repeatable route; 5 s warm-up fills the streaming radius first.
    // Use with -benchmark -fps=60 for a fixed timestep. Screenshots stay outside the CSV capture.
    constexpr float WarmUp = 5.0f;
    BenchElapsed += DeltaSeconds;
    const float End = WarmUp + BenchSeconds;
    const auto Crossed = [this, DeltaSeconds](float Time)
    {
        return BenchElapsed - DeltaSeconds < Time && BenchElapsed >= Time;
    };
    const float Yaw = 70.0f + 35.0f * FMath::Sin(FMath::Min(BenchElapsed, End) * 0.25f);
    if (BenchElapsed < End)
    {
        PC->SetControlRotation(FRotator(-30.0f, Yaw, 0.0f));
        AddMovementInput(GetActorForwardVector(), 1.0f);
        if (CanJump())
        {
            Jump();
        }
    }
    else if (BenchElapsed < End + 0.6f)
    {
        PC->SetControlRotation(FRotator(-2.0f, Yaw, 0.0f)); // eye-level view
    }
    else
    {
        if (Crossed(End + 0.6f))
        {
            // Vista point above the route end, so the horizon screenshot never faces a wall.
            GetCharacterMovement()->SetMovementMode(MOVE_Flying);
            SetActorLocation(GetActorLocation() + FVector(0.0, 0.0, 2500.0));
        }
        PC->SetControlRotation(FRotator(-8.0f, Yaw, 0.0f));
    }
    if (Crossed(WarmUp - 0.5f) || Crossed(End + 0.4f) || Crossed(End + 1.1f))
    {
        PC->ConsoleCommand(TEXT("HighResShot 1"));
    }
    if (!bBenchCapturing && BenchElapsed >= WarmUp && BenchElapsed < End)
    {
        bBenchCapturing = true;
        NextBenchEdit = BenchElapsed;
        PC->ConsoleCommand(TEXT("csvprofile start"));
    }
    if (bBenchCapturing && BenchElapsed >= NextBenchEdit)
    {
        NextBenchEdit += 2.0f;
        EditBlock(false);
    }
    if (bBenchCapturing && BenchElapsed >= End)
    {
        bBenchCapturing = false;
        PC->ConsoleCommand(TEXT("csvprofile stop"));
    }
    if (BenchElapsed >= End + 2.0f) // CSV flush and the final screenshots
    {
        PC->ConsoleCommand(TEXT("quit"));
    }
}

void AAethraCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("MoveForward"), this, &AAethraCharacter::MoveForward);
    Input->BindAxis(TEXT("MoveRight"), this, &AAethraCharacter::MoveRight);
    Input->BindAxis(TEXT("LookYaw"), this, &AAethraCharacter::AddControllerYawInput);
    Input->BindAxis(TEXT("LookPitch"), this, &AAethraCharacter::AddControllerPitchInput);
    Input->BindAction(TEXT("Jump"), IE_Pressed, this, &AAethraCharacter::JumpPressed);
    Input->BindAction(TEXT("Jump"), IE_Released, this, &AAethraCharacter::JumpReleased);
    Input->BindAction(TEXT("Menu"), IE_Pressed, this, &AAethraCharacter::ToggleMenu);
    Input->BindAction(TEXT("RemoveBlock"), IE_Pressed, this, &AAethraCharacter::RemoveBlock);
    Input->BindAction(TEXT("PlaceBlock"), IE_Pressed, this, &AAethraCharacter::PlaceBlock);

    // Hotbar: 1-9 pick a slot, the mouse wheel cycles.
    for (int32 Slot = 0; Slot < FAethraInventory::Slots; ++Slot)
    {
        FInputActionBinding Binding(FName(*FString::Printf(TEXT("Slot%d"), Slot + 1)), IE_Pressed);
        Binding.ActionDelegate.GetDelegateForManualSet().BindLambda([this, Slot] { SelectSlot(Slot); });
        Input->AddActionBinding(Binding);
    }
    const auto BindCycle = [this, Input](const TCHAR* Action, int32 Step)
    {
        FInputActionBinding Binding(FName(Action), IE_Pressed);
        Binding.ActionDelegate.GetDelegateForManualSet().BindLambda([this, Step] { SelectSlot(Inventory.Selected + Step); });
        Input->AddActionBinding(Binding);
    };
    BindCycle(TEXT("SlotNext"), 1);
    BindCycle(TEXT("SlotPrev"), -1);

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
    // Swimming follows the view, so looking down dives.
    AddMovementInput(bSwimming ? Camera->GetForwardVector() : GetActorForwardVector(), Value);
}

void AAethraCharacter::JumpPressed()
{
    bSwimUp = true;
    Jump();
}

void AAethraCharacter::JumpReleased()
{
    bSwimUp = false;
    StopJumping();
}

void AAethraCharacter::UpdateSwimming(AAethraVoxelWorld& Terrain)
{
    UCharacterMovementComponent* Move = GetCharacterMovement();
    const bool bInWater = Terrain.GetBlock(AethraTerrain::CellAt(GetActorLocation())) == EAethraBlock::Water;
    if (bInWater && !bSwimming)
    {
        bSwimming = true;
        Move->SetMovementMode(MOVE_Flying);
        Move->MaxFlySpeed = 300.0f;
        Move->BrakingDecelerationFlying = 1400.0f;
        Move->Velocity *= 0.3f; // the splash takes the speed
    }
    else if (!bInWater && bSwimming)
    {
        bSwimming = false;
        Move->SetMovementMode(MOVE_Falling);
        if (bSwimUp)
        {
            LaunchCharacter(FVector(0, 0, 520), false, true); // climb out onto the bank
        }
    }
    if (bSwimming)
    {
        AddMovementInput(FVector::UpVector, bSwimUp ? 1.0f : -0.25f); // slow sinking unless swimming up
    }
}

void AAethraCharacter::ToggleMenu()
{
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (AAethraHUD* Hud = Cast<AAethraHUD>(PC->GetHUD()))
        {
            Hud->SetMenuOpen(!Hud->IsMenuOpen());
        }
    }
}

bool AAethraCharacter::IsMenuOpen() const
{
    const APlayerController* PC = Cast<APlayerController>(GetController());
    const AAethraHUD* Hud = PC ? Cast<AAethraHUD>(PC->GetHUD()) : nullptr;
    return Hud && Hud->IsMenuOpen();
}

void AAethraCharacter::SaveInventory() const
{
    if (InventorySeed != 0 && BenchSeconds <= 0.0f)
    {
        Inventory.Save(FAethraInventory::PathFor(InventorySeed));
    }
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

void AAethraCharacter::SelectSlot(int32 Slot)
{
    Inventory.Select(Slot);
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
    if (IsMenuOpen())
    {
        return;
    }
    const AAethraGameMode* Mode = GetWorld()->GetAuthGameMode<AAethraGameMode>();
    AAethraVoxelWorld* Terrain = Mode ? Mode->GetVoxelWorld() : nullptr;
    if (!Terrain)
    {
        // Visual World: the same buttons dig into the terrain and the rock, or fill (no blocks there).
        if (AAethraVisualWorld* Visual = Mode ? Mode->GetVisualWorld() : nullptr)
        {
            const FVector From = Camera->GetComponentLocation();
            FHitResult Hit;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(AethraDig), true, this);
            if (GetWorld()->LineTraceSingleByChannel(Hit, From, From + Camera->GetForwardVector() * 800.0, ECC_Visibility, Query) && Hit.GetActor() == Visual)
            {
                Visual->Dig(bPlace ? Hit.ImpactPoint + Hit.ImpactNormal * 60.0 : Hit.ImpactPoint - Hit.ImpactNormal * 40.0, bPlace ? 1.f : 1.3f, bPlace);
            }
        }
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
        const EAethraBlock Dug = Terrain->GetBlock(Target);
        if (!Terrain->SetBlockBy(Target, EAethraBlock::Air, FAethraInstigator::Player()))
        {
            ShowEditFailureUntil = GetWorld()->GetTimeSeconds() + 1.5;
            return;
        }
        // A plant cannot float once its ground is gone.
        const FIntVector Above = Target + FIntVector(0, 0, 1);
        if (AethraTerrain::IsPlant(Terrain->GetBlock(Above)))
        {
            Terrain->SetBlockBy(Above, EAethraBlock::Air, FAethraInstigator::Player());
        }
        if (Inventory.Add(FAethraInventory::DropOf(Dug))) // a full hotbar drops nothing
        {
            SaveInventory();
        }
        return;
    }
    const EAethraBlock ToPlace = Inventory.SelectedBlock();
    if (ToPlace == EAethraBlock::Air)
    {
        ShowEditFailureUntil = GetWorld()->GetTimeSeconds() + 1.5; // empty slot
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
    const EAethraBlock Occupant = Terrain->GetBlock(Destination);
    if (!AethraTerrain::IsReplaceable(Occupant)) // blocks replace air, plants and water
    {
        return;
    }

    const FVector Min = FVector(Destination) * AethraTerrain::CellSizeCm;
    const FBox BlockBounds(Min + FVector(0.1), Min + FVector(AethraTerrain::CellSizeCm - 0.1));
    if (GetCapsuleComponent()->Bounds.GetBox().Intersect(BlockBounds))
    {
        return;
    }
    if (Terrain->SetBlockBy(Destination, ToPlace, FAethraInstigator::Player()))
    {
        Inventory.SpendSelected();
        SaveInventory();
    }
    else
    {
        ShowEditFailureUntil = GetWorld()->GetTimeSeconds() + 1.5;
    }
}
