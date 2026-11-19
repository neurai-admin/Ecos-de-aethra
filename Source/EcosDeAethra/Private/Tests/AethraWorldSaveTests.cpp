#if WITH_DEV_AUTOMATION_TESTS

#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Voxel/AethraWorldSave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVoxelSaveRoundTrip, "Aethra.Voxel.SaveRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraVoxelSaveRoundTrip::RunTest(const FString& Parameters)
{
    auto* Original = NewObject<UAethraWorldSave>();
    Original->Format = 1;
    Original->Seed = 42819;
    Original->Revision = 2;
    FAethraSavedBlock& Removed = Original->Edits.AddDefaulted_GetRef();
    Removed.Cell = FIntVector(-16, 15, 31);
    Removed.Block = EAethraBlock::Air;
    FAethraSavedBlock& Added = Original->Edits.AddDefaulted_GetRef();
    Added.Cell = FIntVector(-17, 15, 31);
    Added.Block = EAethraBlock::Sand;

    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Save encodes to memory"), UGameplayStatics::SaveGameToMemory(Original, Bytes)))
    {
        return false;
    }

    const auto* Loaded = Cast<UAethraWorldSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Save decodes to the same class"), Loaded)
        || !TestEqual(TEXT("Both edits were serialized"), Loaded->Edits.Num(), 2))
    {
        return false;
    }

    TestEqual(TEXT("Format survives"), Loaded->Format, 1);
    TestEqual(TEXT("Seed survives"), Loaded->Seed, 42819);
    TestEqual(TEXT("Revision survives"), Loaded->Revision, int64(2));
    TestTrue(TEXT("Removal survives"), Loaded->Edits[0].Cell == FIntVector(-16, 15, 31)
        && Loaded->Edits[0].Block == EAethraBlock::Air);
    TestTrue(TEXT("Placement survives"), Loaded->Edits[1].Cell == FIntVector(-17, 15, 31)
        && Loaded->Edits[1].Block == EAethraBlock::Sand);
    return true;
}

#endif
