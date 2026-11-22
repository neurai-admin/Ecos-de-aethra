#if WITH_DEV_AUTOMATION_TESTS

#include "AethraInventory.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraInventoryCollectAndSpend, "Aethra.Inventory.CollectAndSpend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraInventoryCollectAndSpend::RunTest(const FString& Parameters)
{
    FAethraInventory Inv;
    TestEqual(TEXT("New inventory places nothing"), Inv.SelectedBlock(), EAethraBlock::Air);
    TestFalse(TEXT("Empty slot cannot be spent"), Inv.SpendSelected());

    TestEqual(TEXT("Plants drop nothing"), FAethraInventory::DropOf(EAethraBlock::Sunflower), EAethraBlock::Air);
    TestEqual(TEXT("Water drops nothing"), FAethraInventory::DropOf(EAethraBlock::Water), EAethraBlock::Air);
    TestEqual(TEXT("Stone drops stone"), FAethraInventory::DropOf(EAethraBlock::Stone), EAethraBlock::Stone);
    TestFalse(TEXT("Air is never collected"), Inv.Add(EAethraBlock::Air));

    TestTrue(TEXT("Dirt collected"), Inv.Add(EAethraBlock::Dirt));
    TestTrue(TEXT("Dirt stacks"), Inv.Add(EAethraBlock::Dirt));
    TestTrue(TEXT("Stone takes the next slot"), Inv.Add(EAethraBlock::Stone));
    TestEqual(TEXT("Slot 1 holds 2 dirt"), Inv.Hotbar[0].Count, 2);
    TestEqual(TEXT("Slot 2 holds stone"), Inv.Hotbar[1].Block, EAethraBlock::Stone);

    TestTrue(TEXT("Spend one dirt"), Inv.SpendSelected());
    TestTrue(TEXT("Spend the last dirt"), Inv.SpendSelected());
    TestEqual(TEXT("Emptied slot is air"), Inv.SelectedBlock(), EAethraBlock::Air);

    Inv.Select(-1);
    TestEqual(TEXT("Selection wraps backwards"), Inv.Selected, FAethraInventory::Slots - 1);
    Inv.Select(FAethraInventory::Slots);
    TestEqual(TEXT("Selection wraps forwards"), Inv.Selected, 0);

    // A full stack overflows into a new slot; a full hotbar refuses.
    FAethraInventory Full;
    for (int32 I = 0; I < FAethraInventory::StackSize * FAethraInventory::Slots; ++I)
    {
        Full.Add(EAethraBlock::Sand);
    }
    TestEqual(TEXT("Last slot is a full stack"), Full.Hotbar[FAethraInventory::Slots - 1].Count, FAethraInventory::StackSize);
    TestFalse(TEXT("Full hotbar refuses"), Full.Add(EAethraBlock::Sand));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraInventorySaveLoad, "Aethra.Inventory.PersistsPerWorld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAethraInventorySaveLoad::RunTest(const FString& Parameters)
{
    const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("InventoryTest.inv"));
    FAethraInventory Inv;
    Inv.Add(EAethraBlock::Marble);
    Inv.Add(EAethraBlock::Marble);
    Inv.Add(EAethraBlock::Planks);
    Inv.Select(1);
    TestTrue(TEXT("Saved"), Inv.Save(Path));

    FAethraInventory Read;
    TestTrue(TEXT("Loaded"), Read.Load(Path));
    TestEqual(TEXT("Stack restored"), Read.Hotbar[0].Count, 2);
    TestEqual(TEXT("Block restored"), Read.Hotbar[1].Block, EAethraBlock::Planks);
    TestEqual(TEXT("Selection restored"), Read.Selected, 1);

    TArray<uint8> Garbage = { 1, 2, 3 };
    FFileHelper::SaveArrayToFile(Garbage, *Path);
    TestFalse(TEXT("Invalid file is rejected"), Read.Load(Path));
    TestEqual(TEXT("Rejected load keeps the inventory"), Read.Hotbar[0].Block, EAethraBlock::Marble);
    TestFalse(TEXT("Missing file is rejected"), Read.Load(Path + TEXT(".missing")));
    TestNotEqual(TEXT("Worlds have their own files"), FAethraInventory::PathFor(1), FAethraInventory::PathFor(2));
    IFileManager::Get().Delete(*Path);
    return true;
}

#endif
