// Menus, career screens and the match HUD, drawn straight to the canvas.
// Positions are in 1920x1080 units and scaled to the window.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "VC26Hud.generated.h"

class AVC26Arena;
namespace vc26 { class Career; class RallySim; class TouchControls; }

UCLASS()
class AVC26Hud : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	float Ui() const;
	float ScreenX(float X) const;
	void Label(const FString& Text, float X, float Y, const FLinearColor& Color, float Size);
	void Panel(float X, float Y, float Width, float Height, const FLinearColor& Color);
	void DrawCentred(const FString& Text, float Y, const FLinearColor& Color, float Size);
	void DrawBar(float Y, float Marker, const TArray<FVector4f>& Zones);
	void DrawCircle(float X, float Y, float Radius, const FLinearColor& Color, float Thickness);
	void DrawTouchControls(const vc26::TouchControls& Touch);
	void DrawMeter(float X, float Y, float Width, float Fill, const FLinearColor& Color);
	void DrawItems(const AVC26Arena& Arena, float X, float Y, float Width);

	void DrawMatch(const AVC26Arena& Arena, const vc26::RallySim& Sim);
	void DrawScoreBug(const AVC26Arena& Arena, const vc26::RallySim& Sim);
	void DrawMainMenu(const AVC26Arena& Arena);
	void DrawCreatePlayer(const AVC26Arena& Arena);
	void DrawHub(const AVC26Arena& Arena, const vc26::Career& Career);
	void DrawTable(const vc26::Career& Career, float X, float Y);
	void DrawUpgrades(const AVC26Arena& Arena, const vc26::Career& Career);
	void DrawReport(const AVC26Arena& Arena, const vc26::Career& Career);
	void DrawSeasonEnd(const vc26::Career& Career);
};
