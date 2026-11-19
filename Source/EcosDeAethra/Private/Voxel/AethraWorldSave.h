#pragma once

#include "AethraVoxelTypes.h"
#include "GameFramework/SaveGame.h"
#include "AethraWorldSave.generated.h"

USTRUCT()
struct FAethraSavedBlock
{
    GENERATED_BODY()

    UPROPERTY()
    FIntVector Cell = FIntVector::ZeroValue;

    UPROPERTY()
    EAethraBlock Block = EAethraBlock::Air;
};

UCLASS()
class UAethraWorldSave : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 Format = 0;

    UPROPERTY()
    int32 Seed = 0;

    UPROPERTY()
    int64 Revision = 0;

    UPROPERTY()
    TArray<FAethraSavedBlock> Edits;
};
