// Score, timing bar, serve gauge and prompts, drawn straight to the canvas.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "VC26Hud.generated.h"

namespace vc26 { class TouchControls; }

UCLASS()
class AVC26Hud : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawCentred(const FString& Text, float Y, const FLinearColor& Color, float Size);
	void DrawBar(float Y, float Marker, const TArray<FVector4f>& Zones);
	void DrawCircle(float X, float Y, float Radius, const FLinearColor& Color, float Thickness);
	void DrawTouchControls(const vc26::TouchControls& Touch);
};
