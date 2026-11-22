#pragma once

#include "GameFramework/HUD.h"
#include "AethraHUD.generated.h"

class UFont;
class UFontFace;

UCLASS()
class AAethraHUD : public AHUD
{
    GENERATED_BODY()

public:
    AAethraHUD();
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;

    // The world menu (Esc, and at the start of a session): continue, the saved worlds, a new world, quit.
    bool IsMenuOpen() const { return bMenuOpen; }
    void SetMenuOpen(bool bOpen);

private:
    // ECOS DE AETHRA's own fonts (Tools/Fonts/make_aethra_fonts.py): Entalhe text, its bold, and Runa titles.
    UPROPERTY()
    TObjectPtr<UFontFace> PixelFace;
    UPROPERTY()
    TObjectPtr<UFontFace> PixelBoldFace;
    UPROPERTY()
    TObjectPtr<UFontFace> RunaFace;
    UPROPERTY(Transient)
    TObjectPtr<UFont> TextFont;
    UPROPERTY(Transient)
    TObjectPtr<UFont> BoldFont;
    UPROPERTY(Transient)
    TObjectPtr<UFont> TitleFont;

    struct FMenuEntry
    {
        FString Label;
        FName Action; // "continue", "quit", "new", or "seed:<n>"
    };
    TArray<FMenuEntry> MenuEntries;
    bool bMenuOpen = false;

    UFont* MakeFont(UFontFace* Face, int32 Points);
    void DrawHotbar();
    void DrawMenu();
    void BuildMenu();
    // A slot or panel frame with cut corners (the carved-stone look of the HUD).
    void DrawCarvedFrame(float X, float Y, float W, float H, float Cut, const FLinearColor& Color, float Thickness);
};
