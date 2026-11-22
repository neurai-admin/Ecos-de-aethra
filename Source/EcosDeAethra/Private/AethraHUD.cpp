#include "AethraHUD.h"

#include "AethraCharacter.h"
#include "AethraGameMode.h"
#include "AethraLisVillage.h"
#include "AethraVoxelWorld.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"
#include "VisualWorld/AethraBlockGen.h"
#include "Voxel/AethraBlockColor.h"

namespace
{
    const TCHAR* BlockName(EAethraBlock Block)
    {
        switch (Block)
        {
        case EAethraBlock::Grass: return TEXT("Grama");
        case EAethraBlock::Dirt: return TEXT("Terra");
        case EAethraBlock::Stone: return TEXT("Pedra");
        case EAethraBlock::Sand: return TEXT("Areia");
        case EAethraBlock::Wood: return TEXT("Tronco");
        case EAethraBlock::Leaves: return TEXT("Folhas");
        case EAethraBlock::PineLeaves: return TEXT("Folhas de pinheiro");
        case EAethraBlock::Snow: return TEXT("Neve");
        case EAethraBlock::BirchWood: return TEXT("Tronco de bétula");
        case EAethraBlock::BirchLeaves: return TEXT("Folhas de bétula");
        case EAethraBlock::Gravel: return TEXT("Cascalho");
        case EAethraBlock::Mud: return TEXT("Lama");
        case EAethraBlock::Basalt: return TEXT("Basalto");
        case EAethraBlock::Limestone: return TEXT("Calcário");
        case EAethraBlock::Sandstone: return TEXT("Arenito");
        case EAethraBlock::Slate: return TEXT("Ardósia");
        case EAethraBlock::Marble: return TEXT("Mármore");
        case EAethraBlock::StoneBricks: return TEXT("Tijolos de pedra");
        case EAethraBlock::Planks: return TEXT("Tábuas");
        case EAethraBlock::RoofTiles: return TEXT("Telhas");
        case EAethraBlock::Aetherite: return TEXT("Aetherita");
        case EAethraBlock::Ceramic: return TEXT("Cerâmica");
        default: return TEXT("");
        }
    }

    constexpr int32 ClassicSeed = 42819;

    FString WorldLabel(int32 Seed)
    {
        if (FAethraBlockGen::IsAethraSeed(Seed))
        {
            return FString::Printf(TEXT("Aethra - semente %d"), FAethraBlockGen::WorldSeedOf(Seed));
        }
        if (Seed == ClassicSeed)
        {
            return TEXT("Mundo clássico");
        }
        if (Seed == FAethraLisVillage::DemoSeed)
        {
            return TEXT("Vila de Elden (demo)");
        }
        return FString::Printf(TEXT("Mundo %d"), Seed);
    }
}

AAethraHUD::AAethraHUD()
{
    static ConstructorHelpers::FObjectFinder<UFontFace> Pixel(TEXT("/Game/UI/Fonts/FF_AethraEntalhe_Regular.FF_AethraEntalhe_Regular"));
    static ConstructorHelpers::FObjectFinder<UFontFace> PixelBold(TEXT("/Game/UI/Fonts/FF_AethraEntalhe_Bold.FF_AethraEntalhe_Bold"));
    static ConstructorHelpers::FObjectFinder<UFontFace> Runa(TEXT("/Game/UI/Fonts/FF_AethraRuna_Regular.FF_AethraRuna_Regular"));
    PixelFace = Pixel.Object;
    PixelBoldFace = PixelBold.Object;
    RunaFace = Runa.Object;
}

void AAethraHUD::BeginPlay()
{
    Super::BeginPlay();
    // The world menu greets a session, except automated runs and a world opened from the menu itself.
    const AGameModeBase* Mode = GetWorld()->GetAuthGameMode();
    const bool bFromMenu = Mode && UGameplayStatics::HasOption(Mode->OptionsString, TEXT("AethraPlay"));
    const TCHAR* Cmd = FCommandLine::Get();
    const bool bAutomated = FParse::Param(Cmd, TEXT("AethraNoMenu")) || FCString::Strifind(Cmd, TEXT("-AethraBench="))
        || FParse::Param(Cmd, TEXT("AethraVWShots"));
    if (!bFromMenu && !bAutomated)
    {
        SetMenuOpen(true);
    }
}

UFont* AAethraHUD::MakeFont(UFontFace* Face, int32 Points)
{
    if (!Face)
    {
        return GEngine ? GEngine->GetSmallFont() : nullptr;
    }
    UFont* Font = NewObject<UFont>(this);
    Font->FontCacheType = EFontCacheType::Runtime;
    Font->LegacyFontSize = Points;
    FTypefaceEntry& Entry = Font->CompositeFont.DefaultTypeface.Fonts.Emplace_GetRef(TEXT("Regular"));
    Entry.Font = FFontData(Face);
    return Font;
}

void AAethraHUD::SetMenuOpen(bool bOpen)
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC || bOpen == bMenuOpen)
    {
        return;
    }
    bMenuOpen = bOpen;
    if (bOpen)
    {
        BuildMenu();
    }
    PC->bShowMouseCursor = bOpen;
    PC->bEnableClickEvents = bOpen;
    PC->SetIgnoreMoveInput(bOpen); // counters: one push when opening, one pop when closing
    PC->SetIgnoreLookInput(bOpen);
    if (bOpen)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        PC->SetInputMode(Mode);
    }
    else
    {
        PC->SetInputMode(FInputModeGameOnly());
    }
}

void AAethraHUD::BuildMenu()
{
    MenuEntries.Reset();
    MenuEntries.Add({ TEXT("Continuar"), TEXT("continue") });
    const AAethraGameMode* Mode = GetWorld()->GetAuthGameMode<AAethraGameMode>();
    const AAethraVoxelWorld* Voxels = Mode ? Mode->GetVoxelWorld() : nullptr;
    if (Voxels)
    {
        // Saved worlds: every seed with a voxel save (AethraWorld_<seed>_<slot>.sav), newest first.
        TArray<FString> Files;
        const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
        IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("AethraWorld_*.sav")), true, false);
        TArray<TPair<FDateTime, int32>> Worlds;
        for (const FString& File : Files)
        {
            const FString Body = FPaths::GetBaseFilename(File).RightChop(12); // after "AethraWorld_"
            FString SeedText, Slot;
            int32 Seed = 0;
            if (Body.Split(TEXT("_"), &SeedText, &Slot, ESearchCase::CaseSensitive, ESearchDir::FromEnd) && LexTryParseString(Seed, *SeedText)
                && !Worlds.ContainsByPredicate([Seed](const TPair<FDateTime, int32>& W) { return W.Value == Seed; }))
            {
                Worlds.Add({ IFileManager::Get().GetTimeStamp(*FPaths::Combine(Dir, File)), Seed });
            }
        }
        Worlds.Sort([](const TPair<FDateTime, int32>& A, const TPair<FDateTime, int32>& B) { return A.Key > B.Key; });
        bool bClassic = false;
        for (int32 I = 0; I < FMath::Min(Worlds.Num(), 6); ++I)
        {
            const int32 Seed = Worlds[I].Value;
            bClassic |= Seed == ClassicSeed;
            MenuEntries.Add({ WorldLabel(Seed) + (Seed == Voxels->Seed ? TEXT("  (atual)") : TEXT("")), FName(*FString::Printf(TEXT("seed:%d"), Seed)) });
        }
        MenuEntries.Add({ TEXT("Novo mundo de Aethra"), TEXT("new") });
        if (!bClassic && Voxels->Seed != ClassicSeed)
        {
            MenuEntries.Add({ TEXT("Mundo clássico"), FName(*FString::Printf(TEXT("seed:%d"), ClassicSeed)) });
        }
    }
    MenuEntries.Add({ TEXT("Sair do jogo"), TEXT("quit") });
}

void AAethraHUD::NotifyHitBoxClick(FName BoxName)
{
    Super::NotifyHitBoxClick(BoxName);
    APlayerController* PC = GetOwningPlayerController();
    if (!bMenuOpen || !PC)
    {
        return;
    }
    const FString Action = BoxName.ToString();
    const AAethraGameMode* Mode = GetWorld()->GetAuthGameMode<AAethraGameMode>();
    const AAethraVoxelWorld* Voxels = Mode ? Mode->GetVoxelWorld() : nullptr;
    int32 Seed = 0;
    if (Action == TEXT("continue"))
    {
        SetMenuOpen(false);
        return;
    }
    if (Action == TEXT("quit"))
    {
        PC->ConsoleCommand(TEXT("quit"));
        return;
    }
    if (Action == TEXT("new"))
    {
        Seed = FAethraBlockGen::MakeSeed(FMath::RandRange(1, 999999));
    }
    else if (!Action.StartsWith(TEXT("seed:")) || !LexTryParseString(Seed, *Action.RightChop(5)))
    {
        return;
    }
    if (Voxels && Seed == Voxels->Seed)
    {
        SetMenuOpen(false);
        return;
    }
    // Every world is the same map with another seed; the voxel world and its saves follow the seed.
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)), true,
        FString::Printf(TEXT("AethraSeed=%d?AethraPlay=1"), Seed));
}

void AAethraHUD::DrawHotbar()
{
    const auto* Character = Cast<AAethraCharacter>(GetOwningPawn());
    if (!Character || !Character->HasInventory())
    {
        return;
    }
    const FAethraInventory& Inv = Character->GetInventory();
    // The belt: a carved panel of 8 cut-corner slots, the chosen one lifted and outlined in gold.
    constexpr float Box = 50.f, Gap = 10.f, Pad = 12.f;
    const float Width = FAethraInventory::Slots * Box + (FAethraInventory::Slots - 1) * Gap;
    const float X0 = FMath::RoundToFloat((Canvas->ClipX - Width) * .5f);
    const float Y0 = Canvas->ClipY - 110.f;
    DrawRect(FLinearColor(.05f, .05f, .04f, .55f), X0 - Pad, Y0 - Pad, Width + 2 * Pad, Box + 2 * Pad);
    DrawCarvedFrame(X0 - Pad, Y0 - Pad, Width + 2 * Pad, Box + 2 * Pad, 14.f, FLinearColor(.6f, .5f, .3f, .7f), 1.f);
    for (int32 I = 0; I < FAethraInventory::Slots; ++I)
    {
        const FAethraInventory::FSlot& S = Inv.Hotbar[I];
        const bool bChosen = I == Inv.Selected;
        const float X = X0 + I * (Box + Gap);
        const float Y = bChosen ? Y0 - 6.f : Y0;
        if (S.Count > 0)
        {
            FLinearColor Swatch = AethraBlockColor(S.Block, FIntVector::ZeroValue, FIntVector(0, 0, 1));
            Swatch.A = 1.f;
            // A cut-corner gem of the block's colour.
            DrawRect(Swatch, X + 14, Y + 8, Box - 28, Box - 16);
            DrawRect(Swatch, X + 8, Y + 14, Box - 16, Box - 28);
            const FString Count = FString::FromInt(S.Count);
            float W = 0.f, H = 0.f;
            GetTextSize(Count, W, H, BoldFont);
            DrawText(Count, FLinearColor::Black, FMath::RoundToFloat(X + Box - W) + 1, FMath::RoundToFloat(Y + Box - H + 5) + 1, BoldFont);
            DrawText(Count, FLinearColor(.97f, .93f, .82f), FMath::RoundToFloat(X + Box - W), FMath::RoundToFloat(Y + Box - H + 5), BoldFont);
        }
        DrawCarvedFrame(X, Y, Box, Box, 10.f, bChosen ? FLinearColor(1.f, .84f, .4f) : FLinearColor(.75f, .68f, .55f, .45f), bChosen ? 3.f : 1.f);
    }
    const EAethraBlock Selected = Inv.SelectedBlock();
    const FString Name = Selected == EAethraBlock::Air ? TEXT("Vazio - cave para coletar blocos") : BlockName(Selected);
    float W = 0.f, H = 0.f;
    GetTextSize(Name, W, H, BoldFont);
    DrawText(Name, FLinearColor::Black, FMath::RoundToFloat((Canvas->ClipX - W) * .5f) + 2, Y0 - 30, BoldFont);
    DrawText(Name, FLinearColor::White, FMath::RoundToFloat((Canvas->ClipX - W) * .5f), Y0 - 32, BoldFont);
    if (Character->IsSwimming())
    {
        DrawText(TEXT("Nadando - espaço sobe, olhe para baixo para mergulhar"), FLinearColor(.7f, .9f, 1.f), 18, Canvas->ClipY - 62, TextFont);
    }
}

void AAethraHUD::DrawCarvedFrame(float X, float Y, float W, float H, float Cut, const FLinearColor& Color, float T)
{
    const FVector2D P[8] = { { X + Cut, Y }, { X + W - Cut, Y }, { X + W, Y + Cut }, { X + W, Y + H - Cut },
                             { X + W - Cut, Y + H }, { X + Cut, Y + H }, { X, Y + H - Cut }, { X, Y + Cut } };
    for (int32 I = 0; I < 8; ++I)
    {
        DrawLine(P[I].X, P[I].Y, P[(I + 1) % 8].X, P[(I + 1) % 8].Y, Color, T);
    }
}

void AAethraHUD::DrawMenu()
{
    DrawRect(FLinearColor(0, 0, 0, .6f), 0, 0, Canvas->ClipX, Canvas->ClipY);
    const FString Title = TEXT("ECOS DE AETHRA");
    float W = 0.f, H = 0.f;
    GetTextSize(Title, W, H, TitleFont);
    const float Cx = Canvas->ClipX * .5f;
    float Y = FMath::RoundToFloat(Canvas->ClipY * .18f);
    DrawText(Title, FLinearColor(0, 0, 0, 1), FMath::RoundToFloat(Cx - W * .5f) + 3, Y + 3, TitleFont);
    DrawText(Title, FLinearColor(.95f, .86f, .55f), FMath::RoundToFloat(Cx - W * .5f), Y, TitleFont);
    Y += H + 40.f;

    float MouseX = -1.f, MouseY = -1.f;
    if (const APlayerController* PC = GetOwningPlayerController())
    {
        PC->GetMousePosition(MouseX, MouseY);
    }
    constexpr float BW = 440.f, BH = 42.f;
    const float X = FMath::RoundToFloat(Cx - BW * .5f);
    for (const FMenuEntry& E : MenuEntries)
    {
        const bool bHover = MouseX >= X && MouseX <= X + BW && MouseY >= Y && MouseY <= Y + BH;
        DrawRect(bHover ? FLinearColor(.22f, .2f, .14f, .92f) : FLinearColor(.08f, .08f, .07f, .85f), X, Y, BW, BH);
        DrawCarvedFrame(X, Y, BW, BH, 10.f, bHover ? FLinearColor(.95f, .8f, .45f) : FLinearColor(.6f, .5f, .3f, .8f), bHover ? 2.f : 1.f);
        GetTextSize(E.Label, W, H, TextFont);
        DrawText(E.Label, FLinearColor::White, FMath::RoundToFloat(Cx - W * .5f), FMath::RoundToFloat(Y + (BH - H) * .5f), TextFont);
        AddHitBox(FVector2D(X, Y), FVector2D(BW, BH), E.Action, true);
        Y += BH + 8.f;
    }
}

void AAethraHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas)
    {
        return;
    }
    if (!TextFont)
    {
        TextFont = MakeFont(PixelFace, 16);
        BoldFont = MakeFont(PixelBoldFace, 16);
        TitleFont = MakeFont(RunaFace, 44);
    }
    if (bMenuOpen)
    {
        DrawMenu();
        return;
    }

    const float X = Canvas->ClipX * 0.5f;
    const float Y = Canvas->ClipY * 0.5f;
    DrawLine(X - 6, Y, X + 6, Y, FLinearColor::Black, 3.0f);
    DrawLine(X, Y - 6, X, Y + 6, FLinearColor::Black, 3.0f);
    DrawLine(X - 6, Y, X + 6, Y, FLinearColor::White, 1.0f);
    DrawLine(X, Y - 6, X, Y + 6, FLinearColor::White, 1.0f);

    if (const auto* Character = Cast<AAethraCharacter>(GetOwningPawn()))
    {
        DrawHotbar();
        const FString Help = Character->HasInventory()
            ? TEXT("1-8 ou roda: escolher | esquerdo: coletar | direito: colocar | Esc: menu | F1-F3 qualidade")
            : TEXT("clique esquerdo escava | direito preenche | Esc: menu | F1-F3 qualidade");
        // Whole-pixel positions keep the pixel font sharp.
        DrawText(Help, FLinearColor::Black, 20, Canvas->ClipY - 34, TextFont);
        DrawText(Help, FLinearColor::White, 18, Canvas->ClipY - 36, TextFont);
        if (Character->HasRecentEditFailure())
        {
            const TCHAR* Why = Character->HasInventory() && Character->GetSelectedBlock() == EAethraBlock::Air
                ? TEXT("Slot vazio") : TEXT("Alteração indisponível");
            DrawText(Why, FLinearColor(1, 0.7f, 0.5f), 18, Canvas->ClipY - 88, BoldFont);
        }
    }
}
