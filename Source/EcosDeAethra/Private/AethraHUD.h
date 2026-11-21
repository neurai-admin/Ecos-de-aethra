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
    virtual void DrawHUD() override;

private:
    // ECOS DE AETHRA's own fonts (Tools/Fonts/make_aethra_fonts.py): pixel text, its bold, and Runa titles.
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

    UFont* MakeFont(UFontFace* Face, int32 Points);
};
