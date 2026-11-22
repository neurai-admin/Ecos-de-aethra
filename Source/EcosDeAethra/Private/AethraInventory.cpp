#include "AethraInventory.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Voxel/AethraTerrain.h"

EAethraBlock FAethraInventory::DropOf(EAethraBlock Dug)
{
    if (Dug == EAethraBlock::Air || Dug == EAethraBlock::Water || AethraTerrain::IsPlant(Dug))
    {
        return EAethraBlock::Air;
    }
    return Dug;
}

bool FAethraInventory::Add(EAethraBlock Block)
{
    if (Block == EAethraBlock::Air)
    {
        return false;
    }
    for (FSlot& S : Hotbar)
    {
        if (S.Count > 0 && S.Block == Block && S.Count < StackSize)
        {
            ++S.Count;
            return true;
        }
    }
    for (FSlot& S : Hotbar)
    {
        if (S.Count == 0)
        {
            S.Block = Block;
            S.Count = 1;
            return true;
        }
    }
    return false;
}

bool FAethraInventory::SpendSelected()
{
    FSlot& S = Hotbar[Selected];
    if (S.Count <= 0)
    {
        return false;
    }
    if (--S.Count == 0)
    {
        S.Block = EAethraBlock::Air;
    }
    return true;
}

FString FAethraInventory::PathFor(int32 WorldSeed)
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), FString::Printf(TEXT("AethraInventory_%d.inv"), WorldSeed));
}

bool FAethraInventory::Save(const FString& Path) const
{
    FBufferArchive Ar;
    uint32 M = Magic;
    int32 N = Slots, Sel = Selected;
    Ar << M << N << Sel;
    for (const FSlot& S : Hotbar)
    {
        uint8 B = static_cast<uint8>(S.Block);
        int32 C = S.Count;
        Ar << B << C;
    }
    return FFileHelper::SaveArrayToFile(Ar, *Path);
}

bool FAethraInventory::Load(const FString& Path)
{
    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *Path, FILEREAD_Silent))
    {
        return false;
    }
    FMemoryReader Ar(Data);
    uint32 M = 0;
    int32 N = 0, Sel = 0;
    Ar << M << N << Sel;
    if (Ar.IsError() || M != Magic || N != Slots)
    {
        return false;
    }
    FAethraInventory Read;
    for (FSlot& S : Read.Hotbar)
    {
        uint8 B = 0;
        int32 C = 0;
        Ar << B << C;
        if (Ar.IsError() || !AethraTerrain::IsKnownBlock(static_cast<EAethraBlock>(B)) || C < 0 || C > StackSize)
        {
            return false;
        }
        S.Block = C > 0 ? static_cast<EAethraBlock>(B) : EAethraBlock::Air;
        S.Count = C;
    }
    Read.Select(Sel);
    *this = Read;
    return true;
}
