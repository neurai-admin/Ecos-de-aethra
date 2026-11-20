#pragma once

#include "AethraVoxelTypes.h"
#include "UObject/Object.h"
#include "AethraBlockChangeRecorder.generated.h"

// Test helper: records every OnBlockChanged broadcast (dynamic delegates need a UFUNCTION).
UCLASS(Transient)
class UAethraBlockChangeRecorder : public UObject
{
    GENERATED_BODY()

public:
    TArray<FAethraBlockChange> Changes;

    UFUNCTION()
    void Record(const FAethraBlockChange& Change) { Changes.Add(Change); }
};
