#include "AethraHUD.h"

#include "AethraCharacter.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

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

void AAethraHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas)
    {
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
        UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
        const FString Help = FString::Printf(
            TEXT("%s | 1-4 blocos | clique esquerdo remove | direito coloca | F1-F3 qualidade"),
            BlockName(Character->GetSelectedBlock()));
        DrawText(Help, FLinearColor::Black, 19, Canvas->ClipY - 27, Font);
        DrawText(Help, FLinearColor::White, 18, Canvas->ClipY - 28, Font);
        if (Character->HasRecentEditFailure())
        {
            DrawText(TEXT("Alteração indisponível"), FLinearColor(1, 0.7f, 0.5f), 18, Canvas->ClipY - 48, Font);
        }
    }
}
