// The player's inventory: a belt of 8 slots. Digging collects the block into it, placing spends one.
// Saved per world seed next to the world's voxel save ("INV1"), so every world keeps its own items.
#pragma once

#include "CoreMinimal.h"
#include "AethraVoxelTypes.h"

struct ECOSDEAETHRA_API FAethraInventory
{
    static constexpr int32 Slots = 8;
    static constexpr int32 StackSize = 99;
    static constexpr uint32 Magic = 0x31564E49; // "INV1"

    struct FSlot
    {
        EAethraBlock Block = EAethraBlock::Air;
        int32 Count = 0;
    };
    FSlot Hotbar[Slots];
    int32 Selected = 0;

    // What digging a block gives (plants and water give nothing).
    static EAethraBlock DropOf(EAethraBlock Dug);
    // Adds one block: the same stack first, then the first empty slot. False when full.
    bool Add(EAethraBlock Block);
    // The block the selected slot would place (Air when empty).
    EAethraBlock SelectedBlock() const { return Hotbar[Selected].Count > 0 ? Hotbar[Selected].Block : EAethraBlock::Air; }
    // Spends one block from the selected slot. False when empty.
    bool SpendSelected();
    void Select(int32 Slot) { Selected = (Slot % Slots + Slots) % Slots; }

    static FString PathFor(int32 WorldSeed);
    bool Save(const FString& Path) const;
    bool Load(const FString& Path); // false (and unchanged) if missing or invalid
};
