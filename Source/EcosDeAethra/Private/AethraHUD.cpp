#include "AethraHUD.h"

#include "AethraCharacter.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "UObject/ConstructorHelpers.h"

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
        default: return TEXT("");
        }
    }
}

AAethraHUD::AAethraHUD()
{
    static ConstructorHelpers::FObjectFinder<UFontFace> Pixel(TEXT("/Game/UI/Fonts/FF_AethraPixel_Regular.FF_AethraPixel_Regular"));
    static ConstructorHelpers::FObjectFinder<UFontFace> PixelBold(TEXT("/Game/UI/Fonts/FF_AethraPixel_Bold.FF_AethraPixel_Bold"));
    static ConstructorHelpers::FObjectFinder<UFontFace> Runa(TEXT("/Game/UI/Fonts/FF_AethraRuna_Regular.FF_AethraRuna_Regular"));
    PixelFace = Pixel.Object;
    PixelBoldFace = PixelBold.Object;
    RunaFace = Runa.Object;
}

UFont* AAethraHUD::MakeFont(UFontFace* Face, int32 Points)
{
    if (!Face)
    {
        return GEngine ? GEngine->GetSmallFont() : nullptr;
    }
    UFont* Font = NewObject<UFont>(this);
    Font->FontCacheType = EFontCacheType::Runtime;
    Font->LegacyFontSize = Points; // 15 pt = a 20 px em: two screen pixels per font pixel, crisp
    FTypefaceEntry& Entry = Font->CompositeFont.DefaultTypeface.Fonts.Emplace_GetRef(TEXT("Regular"));
    Entry.Font = FFontData(Face);
    return Font;
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
        TextFont = MakeFont(PixelFace, 15);
        BoldFont = MakeFont(PixelBoldFace, 15);
    }

    const float X = Canvas->ClipX * 0.5f;
    const float Y = Canvas->ClipY * 0.5f;
    DrawLine(X - 6, Y, X + 6, Y, FLinearColor::Black, 3.0f);
    DrawLine(X, Y - 6, X, Y + 6, FLinearColor::Black, 3.0f);
    DrawLine(X - 6, Y, X + 6, Y, FLinearColor::White, 1.0f);
    DrawLine(X, Y - 6, X, Y + 6, FLinearColor::White, 1.0f);

    if (const auto* Character = Cast<AAethraCharacter>(GetOwningPawn()))
    {
        const FString Help = FString::Printf(
            TEXT("%s | 1-4 blocos | clique esquerdo remove | direito coloca | F1-F3 qualidade"),
            BlockName(Character->GetSelectedBlock()));
        // Whole-pixel positions keep the pixel font sharp.
        DrawText(Help, FLinearColor::Black, 20, Canvas->ClipY - 34, TextFont);
        DrawText(Help, FLinearColor::White, 18, Canvas->ClipY - 36, TextFont);
        if (Character->HasRecentEditFailure())
        {
            DrawText(TEXT("Alteração indisponível"), FLinearColor(1, 0.7f, 0.5f), 18, Canvas->ClipY - 62, BoldFont);
        }
    }
}
