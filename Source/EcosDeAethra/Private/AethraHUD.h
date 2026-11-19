#pragma once

#include "GameFramework/HUD.h"
#include "AethraHUD.generated.h"

UCLASS()
class AAethraHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
};
