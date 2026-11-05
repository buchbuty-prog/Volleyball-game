#include "VC26Hud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "VC26Arena.h"

namespace
{
	constexpr float BarWidth = 560.f;
	constexpr float BarHeight = 26.f;

	// The timing bar shows this many seconds before and after the ideal contact.
	constexpr float BarLead = 0.9f;
	constexpr float BarTail = 0.3f;

	float TimeToBar(float Seconds)
	{
		return (BarLead - Seconds) / (BarLead + BarTail);
	}
}

void AVC26Hud::DrawCentred(const FString& Text, float Y, const FLinearColor& Color, float Size)
{
	UFont* Font = GEngine->GetLargeFont();
	const float Scale = Size * Canvas->ClipY / 1080.f;
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, Font, Scale);
	DrawText(Text, Color, (Canvas->ClipX - Width) / 2.f, Y * Canvas->ClipY / 1080.f, Font, Scale);
}

void AVC26Hud::DrawBar(float Y, float Marker, const TArray<FVector4f>& Zones)
{
	const float Ui = Canvas->ClipY / 1080.f;
	const float Width = BarWidth * Ui;
	const float Left = (Canvas->ClipX - Width) / 2.f;
	const float Top = Y * Ui;
	const float Height = BarHeight * Ui;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), Left, Top, Width, Height);
	// Each zone is x = start, y = end, z = green amount, w = red amount.
	for (const FVector4f& Zone : Zones)
	{
		DrawRect(FLinearColor(Zone.W, Zone.Z, 0.1f, 0.9f), Left + Zone.X * Width, Top, (Zone.Y - Zone.X) * Width, Height);
	}
	DrawRect(FLinearColor::White, Left + FMath::Clamp(Marker, 0.f, 1.f) * Width - 3.f * Ui, Top - 6.f * Ui, 6.f * Ui, Height + 12.f * Ui);
}

void AVC26Hud::DrawHUD()
{
	Super::DrawHUD();

	const AVC26Arena* Arena = Cast<AVC26Arena>(GetOwningPawn());
	const vc26::RallySim* Sim = Arena ? Arena->GetSim() : nullptr;
	if (!Sim)
	{
		return;
	}

	using vc26::Team;
	const vc26::MatchScore& Score = Sim->score();

	DrawCentred(FString::Printf(TEXT("HOME  %d  -  %d  AWAY"), Score.points(Team::Home), Score.points(Team::Away)), 24.f, FLinearColor::White, 2.2f);
	DrawCentred(FString::Printf(TEXT("Sets %d - %d   |   Set %d"), Score.sets(Team::Home), Score.sets(Team::Away), Score.setNumber()), 80.f, FLinearColor(0.8f, 0.8f, 0.8f), 1.2f);

	DrawText(FString::Printf(TEXT("%s  (1 Easy, 2 Normal, 3 Hard)"), Arena->GetDifficultyName()), FLinearColor(0.75f, 0.75f, 0.75f),
		16.f, 16.f, GEngine->GetLargeFont(), Canvas->ClipY / 1080.f);

	if (Arena->GetBannerTimeLeft() > 0.f)
	{
		DrawCentred(Arena->GetBanner(), 190.f, FLinearColor(1.f, 0.9f, 0.2f), 2.6f);
	}

	if (Sim->phase() == vc26::RallyPhase::MatchOver)
	{
		const bool bHomeWon = Score.winner() == Team::Home;
		DrawCentred(bHomeWon ? TEXT("YOU WIN THE MATCH") : TEXT("AWAY WINS THE MATCH"), 440.f, FLinearColor::White, 3.f);
		DrawCentred(TEXT("Press SPACE for a new match"), 530.f, FLinearColor(0.8f, 0.8f, 0.8f), 1.4f);
		return;
	}

	if (!Sim->humanHome())
	{
		return;
	}

	if (Sim->humanServing())
	{
		DrawCentred(TEXT("A / D aim.  Hold SPACE, release in the green zone"), 930.f, FLinearColor::White, 1.3f);
		if (Sim->serveCharging())
		{
			// Red below 0.2 nets the serve; above 0.85 it goes long.
			DrawBar(980.f, Sim->serveGauge(), {FVector4f(0.f, 0.2f, 0.f, 0.8f), FVector4f(0.6f, 0.85f, 0.8f, 0.1f), FVector4f(0.85f, 1.f, 0.f, 0.8f)});
		}
		return;
	}

	const TArray<FVector4f> TimingZones = {
		FVector4f(TimeToBar(0.30f), TimeToBar(-0.30f), 0.3f, 0.8f),
		FVector4f(TimeToBar(0.15f), TimeToBar(-0.15f), 0.7f, 0.5f),
		FVector4f(TimeToBar(0.06f), TimeToBar(-0.06f), 1.f, 0.1f)};

	if (Sim->humanBlocking())
	{
		DrawCentred(TEXT("BLOCK: A / D slides along the net, press SPACE as the hitter strikes"), 930.f, FLinearColor::White, 1.3f);
		DrawBar(980.f, TimeToBar(Sim->block().timeToContact), TimingZones);
		return;
	}

	const vc26::PendingContact& Pending = Sim->pending();
	if (Sim->phase() == vc26::RallyPhase::InPlay && Pending.active && Pending.team == Team::Home)
	{
		const TCHAR* Prompt = Pending.touch == 1 ? TEXT("PASS: get to the ring, press SPACE on the mark")
			: Pending.touch == 2 ? TEXT("SET: A / D picks the hitter, press SPACE on the mark")
			: TEXT("ATTACK: WASD aims, press SPACE on the mark");
		DrawCentred(Prompt, 930.f, FLinearColor::White, 1.3f);
		DrawBar(980.f, TimeToBar(Pending.timeToContact), TimingZones);
	}
}
