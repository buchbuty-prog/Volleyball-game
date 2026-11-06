#include "VC26Hud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "VC26Arena.h"

namespace
{
	constexpr float BarWidth = 560.f;
	constexpr float BarHeight = 26.f;

	constexpr float TextScale = 1.3f;  // the engine's large font is small at 1080p

	const FLinearColor Ink(0.02f, 0.03f, 0.07f, 0.93f);
	const FLinearColor InkLight(0.08f, 0.1f, 0.17f, 0.96f);
	const FLinearColor Gold(1.f, 0.8f, 0.15f);
	const FLinearColor Muted(0.72f, 0.75f, 0.82f);
	const FLinearColor HomeBlue(0.05f, 0.25f, 0.8f, 0.95f);
	const FLinearColor AwayRed(0.72f, 0.08f, 0.08f, 0.95f);

	FString Str(const char* Text)
	{
		return UTF8_TO_TCHAR(Text);
	}

	FString Str(const std::string& Text)
	{
		return UTF8_TO_TCHAR(Text.c_str());
	}

	const TCHAR* ArchetypeBlurb(vc26::Archetype Archetype)
	{
		switch (Archetype)
		{
		case vc26::Archetype::PowerSpiker: return TEXT("Heavy hitter. Best Attack, strong Serve.");
		case vc26::Archetype::DefensiveAnchor: return TEXT("Keeps the ball alive. Best Pass, strong Block.");
		case vc26::Archetype::PrecisionSetter: return TEXT("Runs the offence. Best Set, strong Pass.");
		case vc26::Archetype::BlockSpecialist: return TEXT("Owns the net. Best Block, strong Attack.");
		}
		return TEXT("");
	}
}

float AVC26Hud::Ui() const
{
	return FMath::Min(Canvas->ClipX / 1920.f, Canvas->ClipY / 1080.f);
}

// Keeps the 1920-wide layout centred on windows of other shapes.
float AVC26Hud::ScreenX(float X) const
{
	return (Canvas->ClipX - 1920.f * Ui()) / 2.f + X * Ui();
}

void AVC26Hud::Label(const FString& Text, float X, float Y, const FLinearColor& Color, float Size)
{
	DrawText(Text, Color, ScreenX(X), Y * Ui(), GEngine->GetLargeFont(), Size * TextScale * Ui());
}

void AVC26Hud::Panel(float X, float Y, float Width, float Height, const FLinearColor& Color)
{
	DrawRect(Color, ScreenX(X), Y * Ui(), Width * Ui(), Height * Ui());
}

void AVC26Hud::DrawCentred(const FString& Text, float Y, const FLinearColor& Color, float Size)
{
	UFont* Font = GEngine->GetLargeFont();
	const float Scale = Size * TextScale * Ui();
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, Font, Scale);
	DrawText(Text, Color, (Canvas->ClipX - Width) / 2.f, Y * Ui(), Font, Scale);
}

void AVC26Hud::DrawBar(float Y, float Marker, const TArray<FVector4f>& Zones)
{
	const float Width = BarWidth * Ui();
	const float Left = (Canvas->ClipX - Width) / 2.f;
	const float Top = Y * Ui();
	const float Height = BarHeight * Ui();

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), Left, Top, Width, Height);
	// Each zone is x = start, y = end, z = green amount, w = red amount.
	for (const FVector4f& Zone : Zones)
	{
		DrawRect(FLinearColor(Zone.W, Zone.Z, 0.1f, 0.9f), Left + Zone.X * Width, Top, (Zone.Y - Zone.X) * Width, Height);
	}
	DrawRect(FLinearColor::White, Left + FMath::Clamp(Marker, 0.f, 1.f) * Width - 3.f * Ui(), Top - 6.f * Ui(), 6.f * Ui(), Height + 12.f * Ui());
}

void AVC26Hud::DrawCircle(float X, float Y, float Radius, const FLinearColor& Color, float Thickness)
{
	constexpr int32 Segments = 40;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A = 2.f * PI * i / Segments;
		const float B = 2.f * PI * (i + 1) / Segments;
		DrawLine(X + FMath::Cos(A) * Radius, Y + FMath::Sin(A) * Radius, X + FMath::Cos(B) * Radius, Y + FMath::Sin(B) * Radius, Color, Thickness);
	}
}

void AVC26Hud::DrawTouchControls(const vc26::TouchControls& Touch)
{
	const float Radius = Touch.stickRadius();
	const FLinearColor Rim(1.f, 1.f, 1.f, 0.55f);
	const FLinearColor Lit(1.f, 0.9f, 0.2f, 0.95f);

	// The stick appears under the thumb. Until then, this shows where to put it.
	const float StickX = Touch.stickActive() ? Touch.stickOriginX() : 0.16f * Canvas->ClipX;
	const float StickY = Touch.stickActive() ? Touch.stickOriginY() : 0.72f * Canvas->ClipY;
	DrawCircle(StickX, StickY, Radius, Rim, 3.f);
	DrawCircle(StickX + Touch.stickX() * Radius, StickY + Touch.stickY() * Radius, Radius * 0.42f, Touch.stickActive() ? Lit : Rim, 4.f);

	const float ButtonX = 0.84f * Canvas->ClipX;
	const float ButtonY = 0.72f * Canvas->ClipY;
	const bool bHeld = Touch.actionHeld();
	DrawCircle(ButtonX, ButtonY, Radius, bHeld ? Lit : Rim, bHeld ? 6.f : 3.f);
	DrawCircle(ButtonX, ButtonY, Radius * 0.42f, bHeld ? Lit : Rim, bHeld ? 6.f : 3.f);
}

void AVC26Hud::DrawMeter(float X, float Y, float Width, float Fill, const FLinearColor& Color)
{
	Panel(X, Y, Width, 14.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Panel(X, Y, Width * FMath::Clamp(Fill, 0.f, 1.f), 14.f, Color);
}

void AVC26Hud::DrawItems(const AVC26Arena& Arena, float X, float Y, float Width)
{
	const TArray<FVC26MenuItem> Items = Arena.GetMenuItems();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const bool bSelected = i == Arena.GetMenuIndex();
		const float Top = Y + i * 66.f;
		Panel(X, Top, Width, 56.f, bSelected ? Gold : Ink);
		Label(Items[i].Label, X + 22.f, Top + 12.f, bSelected ? FLinearColor(0.02f, 0.03f, 0.07f) : FLinearColor::White, 1.5f);
	}
}

void AVC26Hud::DrawMainMenu(const AVC26Arena& Arena)
{
	Panel(0.f, 0.f, 620.f, 1080.f, Ink);
	Panel(620.f, 0.f, 6.f, 1080.f, Gold);
	Label(TEXT("VOLLEYBALL"), 60.f, 140.f, FLinearColor::White, 3.6f);
	Label(TEXT("CAREER 26"), 60.f, 220.f, Gold, 3.6f);
	DrawItems(Arena, 60.f, 400.f, 500.f);
	Label(TEXT("Up / Down to choose, Enter to confirm"), 60.f, 980.f, Muted, 1.1f);
}

void AVC26Hud::DrawCreatePlayer(const AVC26Arena& Arena)
{
	const vc26::Archetype Chosen = Arena.GetChosenArchetype();
	const vc26::SkillRatings Skills = vc26::Career::startingSkills(Chosen);

	Panel(460.f, 170.f, 1000.f, 740.f, Ink);
	Panel(460.f, 170.f, 1000.f, 8.f, Gold);
	DrawCentred(TEXT("CREATE YOUR PLAYER"), 196.f, FLinearColor::White, 2.f);
	const FString& Typed = Arena.GetTypedName();
	DrawCentred(Typed.IsEmpty() ? FString(TEXT("Type your name")) : Typed + TEXT("_"), 262.f, Typed.IsEmpty() ? Muted : FLinearColor::White, 1.5f);
	DrawCentred(FString::Printf(TEXT("<   %s   >"), *Str(vc26::toString(Chosen))), 320.f, Gold, 2.2f);
	DrawCentred(ArchetypeBlurb(Chosen), 392.f, Muted, 1.2f);

	for (int32 i = 0; i < vc26::kSkills; ++i)
	{
		const float Y = 460.f + i * 62.f;
		const int32 Value = Skills[i];
		Label(Str(vc26::toString(static_cast<vc26::Skill>(i))), 560.f, Y, FLinearColor::White, 1.4f);
		DrawMeter(760.f, Y + 14.f, 480.f, vc26::skillFill(Value), Value > 55 ? Gold : FLinearColor(0.35f, 0.5f, 0.8f));
		Label(FString::FromInt(Value), 1270.f, Y, FLinearColor::White, 1.4f);
	}
	DrawCentred(TEXT("You play only your own position. Left / Right arrows to choose, Enter to start"), 820.f, Muted, 1.2f);
}

void AVC26Hud::DrawTable(const vc26::Career& Career, float X, float Y)
{
	Panel(X, Y, 860.f, 590.f, Ink);
	Label(TEXT("LEAGUE TABLE"), X + 24.f, Y + 18.f, Gold, 1.5f);
	Label(TEXT("P"), X + 470.f, Y + 26.f, Muted, 1.f);
	Label(TEXT("W"), X + 530.f, Y + 26.f, Muted, 1.f);
	Label(TEXT("L"), X + 592.f, Y + 26.f, Muted, 1.f);
	Label(TEXT("Sets"), X + 660.f, Y + 26.f, Muted, 1.f);
	Label(TEXT("Pts"), X + 770.f, Y + 26.f, Muted, 1.f);

	const auto Order = Career.standings();
	for (int32 Row = 0; Row < vc26::Career::kTeams; ++Row)
	{
		const int32 Index = Order[Row];
		const vc26::LeagueTeam& Team = Career.teams()[Index];
		const float Top = Y + 76.f + Row * 62.f;
		const bool bMine = Index == 0;
		Panel(X + 12.f, Top, 836.f, 54.f, bMine ? FLinearColor(0.12f, 0.2f, 0.45f, 0.95f) : InkLight);
		if (Row < 2)
		{
			Panel(X + 12.f, Top, 6.f, 54.f, Gold);  // final places
		}
		const FLinearColor Text = bMine ? FLinearColor::White : FLinearColor(0.88f, 0.9f, 0.95f);
		Label(FString::Printf(TEXT("%d   %s"), Row + 1, *Str(Team.name)), X + 32.f, Top + 12.f, Text, 1.25f);
		Label(FString::FromInt(Team.played), X + 470.f, Top + 14.f, Text, 1.15f);
		Label(FString::FromInt(Team.won), X + 530.f, Top + 14.f, Text, 1.15f);
		Label(FString::FromInt(Team.lost), X + 592.f, Top + 14.f, Text, 1.15f);
		Label(FString::Printf(TEXT("%d-%d"), Team.setsFor, Team.setsAgainst), X + 660.f, Top + 14.f, Text, 1.15f);
		Label(FString::FromInt(Team.points), X + 770.f, Top + 14.f, Gold, 1.25f);
	}
}

void AVC26Hud::DrawHub(const AVC26Arena& Arena, const vc26::Career& Career)
{
	Panel(0.f, 0.f, 1920.f, 130.f, Ink);
	Panel(0.f, 130.f, 1920.f, 5.f, Gold);
	Label(Str(Career.name()), 60.f, 26.f, FLinearColor::White, 2.2f);
	Label(FString::Printf(TEXT("%s   |   Overall %d   |   %s"), *Str(vc26::toString(Career.archetype())), Career.overall(), *Str(Career.myTeam().name)), 60.f, 84.f, Muted, 1.2f);
	Label(FString::Printf(TEXT("%s - Season %d"), *Str(vc26::toString(Career.tier())), Career.season()), 1260.f, 30.f, Gold, 1.6f);
	const FString Progress = Career.stage() == vc26::CareerStage::Final ? FString(TEXT("The Final"))
		: FString::Printf(TEXT("Round %d of %d"), Career.round() + 1, vc26::Career::kRounds);
	Label(Progress, 1260.f, 84.f, Muted, 1.2f);

	// Next match.
	const vc26::LeagueTeam& Rival = Career.teams()[Career.nextOpponent()];
	Panel(60.f, 180.f, 800.f, 200.f, Ink);
	Label(Career.isRivalMatch() ? FString::Printf(TEXT("RIVAL MATCH: %s"), *Str(vc26::Career::rivalName())) : FString(TEXT("NEXT MATCH")), 84.f, 198.f, Gold, 1.3f);
	Label(FString::Printf(TEXT("vs  %s"), *Str(Rival.name)), 84.f, 244.f, FLinearColor::White, 2.f);
	Label(FString::Printf(TEXT("Strength %d   |   %d-%d this season"), Rival.strength, Rival.won, Rival.lost), 84.f, 318.f, Muted, 1.2f);
	if (!Career.canPlay())
	{
		const FString Status = Career.injuredMatches() > 0
			? FString::Printf(TEXT("INJURED: out for %d more %s"), Career.injuredMatches(), Career.injuredMatches() == 1 ? TEXT("match") : TEXT("matches"))
			: FString(TEXT("BENCHED: the coach has lost trust in you"));
		Label(Status, 84.f, 350.f, FLinearColor(0.95f, 0.4f, 0.35f), 1.05f);
	}

	// Scouts.
	Panel(60.f, 400.f, 800.f, 130.f, Ink);
	Label(FString::Printf(TEXT("SCOUT INTEREST  %d%%"), Career.scoutInterest()), 84.f, 418.f, Gold, 1.3f);
	DrawMeter(84.f, 468.f, 750.f, Career.scoutInterest() / 100.f, Gold);
	Label(TEXT("Reach 100% to earn a move up, even without the title"), 84.f, 490.f, Muted, 1.f);

	DrawItems(Arena, 60.f, 560.f, 800.f);
	Label(FString::Printf(TEXT("Skill points: %d      Average rating: %.1f      Trophies: %d"), Career.skillPoints(), Career.averageRating(), Career.trophies()), 60.f, 836.f, FLinearColor::White, 1.2f);
	Label(TEXT("Coach trust"), 60.f, 882.f, Muted, 1.05f);
	DrawMeter(280.f, 892.f, 580.f, Career.coachTrust() / 100.f, FLinearColor(0.3f, 0.75f, 0.45f));
	Label(TEXT("Chemistry"), 60.f, 920.f, Muted, 1.05f);
	DrawMeter(280.f, 930.f, 580.f, Career.chemistry() / 100.f, FLinearColor(0.35f, 0.55f, 0.95f));
	Label(FString::Printf(TEXT("vs %s: %d-%d"), *Str(vc26::Career::rivalName()), Career.rivalWins(), Career.rivalLosses()), 60.f, 958.f, Muted, 1.05f);

	DrawTable(Career, 1000.f, 180.f);
}

void AVC26Hud::DrawUpgrades(const AVC26Arena& Arena, const vc26::Career& Career)
{
	Panel(360.f, 130.f, 1200.f, 820.f, Ink);
	Panel(360.f, 130.f, 1200.f, 8.f, Gold);
	DrawCentred(TEXT("UPGRADE SKILLS"), 164.f, FLinearColor::White, 2.2f);
	DrawCentred(FString::Printf(TEXT("%d skill points to spend"), Career.skillPoints()), 240.f, Gold, 1.5f);

	const TArray<FVC26MenuItem> Items = Arena.GetMenuItems();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const float Y = 320.f + i * 92.f;
		const bool bSelected = i == Arena.GetMenuIndex();
		Panel(420.f, Y, 1080.f, 78.f, bSelected ? FLinearColor(0.2f, 0.17f, 0.04f, 0.95f) : InkLight);
		if (bSelected)
		{
			Panel(420.f, Y, 8.f, 78.f, Gold);
		}
		Label(Items[i].Label, 450.f, Y + 22.f, FLinearColor::White, 1.5f);
		if (i < vc26::kSkills)
		{
			const vc26::Skill Skill = static_cast<vc26::Skill>(i);
			const int32 Value = Career.skills()[i];
			const int32 Cost = Career.upgradeCost(Skill);
			DrawMeter(660.f, Y + 32.f, 500.f, vc26::skillFill(Value), Gold);
			Label(FString::FromInt(Value), 1190.f, Y + 20.f, FLinearColor::White, 1.6f);
			Label(FString::Printf(TEXT("Cost %d"), Cost), 1300.f, Y + 24.f, Career.skillPoints() >= Cost ? Gold : Muted, 1.25f);
		}
	}
}

void AVC26Hud::DrawReport(const AVC26Arena& Arena, const vc26::Career* Career)
{
	const vc26::MatchReport& Report = Arena.GetReport();
	const vc26::MatchStats& Stats = Report.stats;

	Panel(360.f, 130.f, 1200.f, 820.f, Ink);
	Panel(360.f, 130.f, 1200.f, 8.f, Report.won ? Gold : FLinearColor(0.6f, 0.1f, 0.1f));
	DrawCentred(Report.won ? TEXT("VICTORY") : TEXT("DEFEAT"), 164.f, Report.won ? Gold : FLinearColor(0.95f, 0.4f, 0.35f), 3.f);
	DrawCentred(FString::Printf(TEXT("Sets %d - %d"), Report.setsFor, Report.setsAgainst), 262.f, FLinearColor::White, 1.5f);

	if (Report.satOut)
	{
		DrawCentred(TEXT("You watched this one."), 400.f, Muted, 1.8f);
		DrawCentred(FString::Printf(TEXT("+%d skill point from film and gym work"), Report.skillPoints), 500.f, FLinearColor::White, 1.4f);
		if (Career)
		{
			DrawCentred(FString::Printf(TEXT("League position: %d"), Career->leaguePosition()), 560.f, FLinearColor::White, 1.4f);
		}
		DrawCentred(TEXT("Press Enter to continue"), 870.f, Muted, 1.2f);
		return;
	}

	Label(TEXT("MATCH RATING"), 440.f, 340.f, Muted, 1.2f);
	Label(FString::Printf(TEXT("%.1f"), Report.rating), 440.f, 380.f, Report.rating >= 7.f ? Gold : FLinearColor::White, 5.f);
	if (Career)
	{
		Label(FString::Printf(TEXT("+%d skill points"), Report.skillPoints), 440.f, 540.f, FLinearColor::White, 1.4f);
		Label(FString::Printf(TEXT("+%d scout interest"), Report.scoutGain), 440.f, 590.f, FLinearColor::White, 1.4f);
		Label(FString::Printf(TEXT("League position: %d"), Career->leaguePosition()), 440.f, 640.f, FLinearColor::White, 1.4f);
	}

	const TPair<const TCHAR*, int32> Lines[] = {
		{TEXT("Perfect touches"), Stats.perfect}, {TEXT("Good touches"), Stats.good}, {TEXT("Shanks"), Stats.shanks},
		{TEXT("Kills"), Stats.kills}, {TEXT("Aces"), Stats.aces}, {TEXT("Stuff blocks"), Stats.blocks},
		{TEXT("Clutch points"), Stats.clutchPoints}, {TEXT("Errors"), Stats.errors}};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Lines); ++i)
	{
		const float Y = 340.f + i * 52.f;
		Label(Lines[i].Key, 900.f, Y, Muted, 1.3f);
		Label(FString::FromInt(Lines[i].Value), 1380.f, Y, FLinearColor::White, 1.4f);
	}
	DrawCentred(Arena.HasHighlights() ? TEXT("Press H for highlights, Enter to continue") : TEXT("Press Enter to continue"), 870.f, Muted, 1.2f);
}

void AVC26Hud::DrawStory(const AVC26Arena& Arena, const vc26::Career& Career)
{
	const vc26::StoryEvent* Story = Career.pendingStory();
	if (!Story)
	{
		return;
	}
	Panel(360.f, 220.f, 1200.f, 620.f, Ink);
	Panel(360.f, 220.f, 1200.f, 8.f, Gold);
	DrawCentred(Str(Story->title), 262.f, Gold, 2.2f);
	DrawCentred(Str(Story->text), 380.f, FLinearColor::White, 1.2f);
	DrawItems(Arena, 460.f, 500.f, 1000.f);
	DrawCentred(TEXT("Your answer changes how the coach and the team see you"), 720.f, Muted, 1.f);
}

void AVC26Hud::DrawSeasonEnd(const vc26::Career& Career)
{
	Panel(360.f, 170.f, 1200.f, 740.f, Ink);
	Panel(360.f, 170.f, 1200.f, 8.f, Gold);
	DrawCentred(FString::Printf(TEXT("SEASON %d COMPLETE"), Career.season()), 210.f, FLinearColor::White, 2.4f);

	const FString Headline = Career.wonFinal() ? FString(TEXT("CHAMPIONS!")) : FString::Printf(TEXT("Finished %d of %d"), Career.leaguePosition(), vc26::Career::kTeams);
	DrawCentred(Headline, 320.f, Gold, 3.f);
	DrawCentred(FString::Printf(TEXT("%s, %s"), *Str(Career.myTeam().name), *Str(vc26::toString(Career.tier()))), 430.f, Muted, 1.3f);
	DrawCentred(FString::Printf(TEXT("Record %d-%d   |   Average rating %.1f   |   Trophies %d"), Career.myTeam().won, Career.myTeam().lost, Career.averageRating(), Career.trophies()), 500.f, FLinearColor::White, 1.4f);

	FString Next = TEXT("Another season at this level. Go again.");
	if (Career.promoted())
	{
		Next = Career.tier() == vc26::Tier::University ? TEXT("The National League has signed you!") : TEXT("You've been called up to the National Team!");
	}
	else if (Career.tier() == vc26::Tier::WorldStage && Career.wonFinal())
	{
		Next = TEXT("World champions. Defend your title next season.");
	}
	DrawCentred(Next, 620.f, Career.promoted() ? Gold : FLinearColor::White, 1.8f);
	if (!Career.history().empty() && Career.history().back().mvp)
	{
		DrawCentred(TEXT("League MVP"), 710.f, Gold, 2.f);
	}
	DrawCentred(Career.offers().empty() ? TEXT("Press Enter for next season") : TEXT("Press Enter: clubs are calling"), 820.f, Muted, 1.2f);
}

void AVC26Hud::DrawTransfers(const AVC26Arena& Arena, const vc26::Career& Career)
{
	Panel(360.f, 170.f, 1200.f, 740.f, Ink);
	Panel(360.f, 170.f, 1200.f, 8.f, Gold);
	DrawCentred(TEXT("TRANSFER WINDOW"), 210.f, FLinearColor::White, 2.4f);
	DrawCentred(TEXT("A new club means a new coach and a locker room to win over"), 300.f, Muted, 1.2f);
	DrawItems(Arena, 460.f, 380.f, 1000.f);

	const TArray<FVC26MenuItem> Items = Arena.GetMenuItems();
	if (Items.IsValidIndex(Arena.GetMenuIndex()) && Items[Arena.GetMenuIndex()].Arg >= 0)
	{
		const vc26::TransferOffer& Offer = Career.offers()[Items[Arena.GetMenuIndex()].Arg];
		DrawCentred(FString::Printf(TEXT("Squad strength +%d   |   +%d skill points on signing"), Offer.strength, Offer.signingPoints), 680.f, Gold, 1.4f);
	}
	else
	{
		DrawCentred(TEXT("Loyalty lifts the locker room"), 680.f, FLinearColor::White, 1.4f);
	}
}

void AVC26Hud::DrawRecord(const AVC26Arena& Arena, const vc26::Career& Career)
{
	Panel(200.f, 90.f, 1520.f, 900.f, Ink);
	Panel(200.f, 90.f, 1520.f, 8.f, Gold);
	DrawCentred(FString::Printf(TEXT("%s - CAREER RECORD"), *Str(Career.name())), 120.f, FLinearColor::White, 2.f);
	DrawCentred(FString::Printf(TEXT("%d matches   |   %d kills   |   %d aces   |   %d stuff blocks   |   Best rating %.1f   |   Trophies %d"),
		Career.matchesPlayed(), Career.careerKills(), Career.careerAces(), Career.careerBlocks(), Career.bestRating(), Career.trophies()), 190.f, Gold, 1.15f);

	const std::vector<vc26::SeasonRecord>& History = Career.history();
	if (History.empty())
	{
		DrawCentred(TEXT("Your first season is still being written."), 420.f, Muted, 1.5f);
	}
	const float Columns[] = {250.f, 370.f, 860.f, 1140.f, 1290.f, 1420.f};
	const TCHAR* const Titles[] = {TEXT("Yr"), TEXT("Club"), TEXT("Level"), TEXT("Place"), TEXT("W-L"), TEXT("Rating")};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Columns) && !History.empty(); ++i)
	{
		Label(Titles[i], Columns[i], 250.f, Muted, 1.1f);
	}

	// The most recent seasons, newest first.
	const int32 Shown = FMath::Min(static_cast<int32>(History.size()), 9);
	for (int32 i = 0; i < Shown; ++i)
	{
		const vc26::SeasonRecord& Season = History[History.size() - 1 - i];
		const float Y = 296.f + i * 62.f;
		Panel(230.f, Y - 6.f, 1460.f, 54.f, InkLight);
		Label(FString::FromInt(Season.season), Columns[0], Y, FLinearColor::White, 1.2f);
		Label(Str(Season.club), Columns[1], Y, FLinearColor::White, 1.2f);
		Label(Str(vc26::toString(Season.tier)), Columns[2], Y, Muted, 1.05f);
		Label(Season.champion ? FString(TEXT("Champions")) : FString::FromInt(Season.position), Columns[3], Y, Season.champion ? Gold : FLinearColor::White, Season.champion ? 0.9f : 1.2f);
		Label(FString::Printf(TEXT("%d-%d"), Season.won, Season.lost), Columns[4], Y, FLinearColor::White, 1.2f);
		Label(FString::Printf(TEXT("%.1f%s"), Season.rating, Season.mvp ? TEXT("  MVP") : TEXT("")), Columns[5], Y, Season.mvp ? Gold : FLinearColor::White, 1.2f);
	}
	DrawItems(Arena, 760.f, 900.f, 400.f);
}

void AVC26Hud::DrawScoreBug(const AVC26Arena& Arena, const vc26::RallySim& Sim)
{
	using vc26::Team;
	const vc26::MatchScore& Score = Sim.score();

	const float Left = 40.f;
	const float Top = 30.f;
	auto Row = [&](float Y, const FString& Name, const FLinearColor& Color, Team Side)
	{
		Panel(Left, Y, 520.f, 50.f, Ink);
		Panel(Left, Y, 10.f, 50.f, Color);
		Label(Name, Left + 26.f, Y + 12.f, FLinearColor::White, 1.2f);
		Label(FString::FromInt(Score.sets(Side)), Left + 410.f, Y + 12.f, Gold, 1.25f);
		Panel(Left + 450.f, Y, 70.f, 50.f, Color);
		Label(FString::FromInt(Score.points(Side)), Left + 464.f, Y + 8.f, FLinearColor::White, 1.6f);
		if (Sim.servingTeam() == Side)
		{
			Panel(Left + 384.f, Y + 19.f, 12.f, 12.f, FLinearColor::White);
		}
	};
	Row(Top, Arena.GetHomeName(), HomeBlue, Team::Home);
	Row(Top + 54.f, Arena.GetAwayName(), AwayRed, Team::Away);

	// Momentum grows out from the centre towards the team that has it.
	const float Centre = Left + 260.f;
	const float Momentum = Sim.momentum();
	const bool bZone = Sim.inZone(Team::Home) || Sim.inZone(Team::Away);
	Panel(Left, Top + 108.f, 520.f, 12.f, Ink);
	Panel(Momentum < 0.f ? Centre + Momentum * 260.f : Centre, Top + 108.f, FMath::Abs(Momentum) * 260.f, 12.f, bZone ? Gold : Momentum < 0.f ? AwayRed : HomeBlue);
	Panel(Centre - 1.f, Top + 104.f, 2.f, 20.f, FLinearColor::White);
}

void AVC26Hud::DrawMatch(const AVC26Arena& Arena, const vc26::RallySim& Sim)
{
	using vc26::Team;
	if (Arena.IsReplaying())
	{
		// Letterboxed, so a replay can never be mistaken for live play.
		Panel(0.f, 0.f, 1920.f, 96.f, FLinearColor::Black);
		Panel(0.f, 984.f, 1920.f, 96.f, FLinearColor::Black);
		Label(TEXT("REPLAY"), 60.f, 24.f, Gold, 2.f);
		DrawCentred(TEXT("Press SPACE to skip"), 1010.f, Muted, 1.1f);
		return;
	}
	DrawScoreBug(Arena, Sim);

	if (Arena.GetCommentaryTimeLeft() > 0.f)
	{
		const float Fade = FMath::Min(Arena.GetCommentaryTimeLeft() / 0.4f, 1.f);
		Panel(360.f, 1016.f, 1200.f, 44.f, FLinearColor(0.03f, 0.05f, 0.1f, 0.8f * Fade));
		DrawCentred(Arena.GetCommentary(), 1024.f, FLinearColor(1.f, 1.f, 1.f, Fade), 1.05f);
	}
	if (Sim.humanHome() && Sim.phase() != vc26::RallyPhase::MatchOver)
	{
		Label(Arena.PlayerLabel(Sim.controlled()), 40.f, 1024.f, Gold, 1.05f);
	}
	if (Sim.phase() == vc26::RallyPhase::PreServe)
	{
		Label(FString::Printf(TEXT("Serving: %s"), *Arena.PlayerLabel(Sim.server())), 1500.f, 1024.f, FLinearColor::White, 1.05f);
	}

	const vc26::TouchControls* Touch = Arena.GetTouch();
	const bool bTouch = Touch != nullptr;
	if (Sim.beach())
	{
		const vc26::Vec3 Wind = Sim.wind();
		Label(FString::Printf(TEXT("Wind %.1f m/s %s"), FMath::Sqrt(Wind.x * Wind.x + Wind.y * Wind.y), Wind.y > 0.f ? TEXT("<<<") : TEXT(">>>")), 1500.f, 40.f, FLinearColor::White, 1.2f);
	}
	if (Arena.IsCouchMatch())
	{
		Label(TEXT("P1: WASD + Space    P2: Arrows + Enter, or gamepad"), 40.f, 150.f, Muted, 1.f);
	}
	else if (!Arena.IsCareerMatch() && Sim.humanHome())
	{
		Label(FString::Printf(TEXT("%s  %s"), Arena.GetDifficultyName(), bTouch ? TEXT("(tap to change)") : TEXT("(1 Easy, 2 Normal, 3 Hard)")), 40.f, 150.f, Muted, bTouch ? 1.6f : 1.f);
	}
	if (Sim.inZone(Team::Home))
	{
		Label(TEXT("IN THE ZONE: timing windows wider"), 40.f, 190.f, Gold, 1.f);
	}
	if (bTouch)
	{
		DrawTouchControls(*Touch);
	}

	if (Arena.GetBannerTimeLeft() > 0.f)
	{
		DrawCentred(Arena.GetBanner(), 190.f, FLinearColor(1.f, 0.9f, 0.2f), 2.6f);
	}

	if (Sim.phase() == vc26::RallyPhase::MatchOver)
	{
		const bool bHomeWon = Sim.score().winner() == Team::Home;
		const FString Result = Arena.IsCouchMatch() ? (bHomeWon ? Arena.GetHomeName() : Arena.GetAwayName()) + TEXT(" WINS") : FString(bHomeWon ? TEXT("YOU WIN THE MATCH") : TEXT("YOU LOST THE MATCH"));
		DrawCentred(Result, 440.f, FLinearColor::White, 3.f);
		DrawCentred(bTouch ? TEXT("Tap the right side to continue") : TEXT("Press SPACE to continue"), 530.f, FLinearColor(0.8f, 0.8f, 0.8f), 1.4f);
		return;
	}

	if (!Sim.humanHome())
	{
		return;
	}

	if (Sim.humanServing())
	{
		DrawCentred(bTouch ? TEXT("Left thumb aims. Hold the right side, release in the green") : TEXT("A / D aim.  Hold SPACE, release in the green zone"), 930.f, FLinearColor::White, 1.1f);
		if (Sim.serveCharging())
		{
			// Red below 0.2 nets the serve; above 0.85 it goes long.
			// Nerves: at set point the marker trembles as well as running faster.
			const float Shake = Sim.pressurePoint() ? FMath::Sin(GetWorld()->GetTimeSeconds() * 47.f) * 0.012f : 0.f;
			DrawBar(980.f, Sim.serveGauge() + Shake, {FVector4f(0.f, 0.2f, 0.f, 0.8f), FVector4f(0.6f, 0.85f, 0.8f, 0.1f), FVector4f(0.85f, 1.f, 0.f, 0.8f)});
		}
		return;
	}

	// Timing lives on the court, in the closing ring, so eyes can stay on the play.
	if (Sim.humanBlocking())
	{
		DrawCentred(bTouch ? TEXT("BLOCK: slide along the net, tap as the rings meet") : TEXT("BLOCK: A / D slides along the net, SPACE to jump as the rings meet"), 985.f, FLinearColor::White, 1.1f);
		return;
	}

	const vc26::PendingContact& Pending = Sim.pending();
	if (Sim.phase() == vc26::RallyPhase::InPlay && Sim.humanToPlay())
	{
		const TCHAR* Prompt = Pending.touch == 1 ? (bTouch ? TEXT("PASS: tap as the rings meet") : TEXT("PASS: steer to the ring, SPACE as the rings meet"))
			: Pending.touch == 2 ? (bTouch ? TEXT("SET: left thumb calls the play, tap as the rings meet") : TEXT("SET: A / D outside, W quick, S dump. SPACE as the rings meet"))
			: Pending.spike ? (bTouch ? TEXT("ATTACK: left thumb aims, tap to jump as the rings meet") : TEXT("ATTACK: WASD aims, SPACE to jump as the rings meet"))
			: (bTouch ? TEXT("Tap as the rings meet to play it over") : TEXT("SPACE as the rings meet to play it over"));
		DrawCentred(Prompt, 985.f, FLinearColor::White, 1.1f);
	}
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

	// Dims the arena behind the career screens so they stay readable.
	if (Arena->GetScreen() != EVC26Screen::MainMenu && Arena->GetScreen() != EVC26Screen::Match)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	}

	const vc26::Career* Career = Arena->GetCareer();
	switch (Arena->GetScreen())
	{
	case EVC26Screen::MainMenu: DrawMainMenu(*Arena); break;
	case EVC26Screen::CreatePlayer: DrawCreatePlayer(*Arena); break;
	case EVC26Screen::Match: DrawMatch(*Arena, *Sim); break;
	case EVC26Screen::Hub: if (Career) { DrawHub(*Arena, *Career); } break;
	case EVC26Screen::Upgrades: if (Career) { DrawUpgrades(*Arena, *Career); } break;
	case EVC26Screen::Report: DrawReport(*Arena, Arena->IsReportFromCareer() ? Career : nullptr); break;
	case EVC26Screen::SeasonEnd: if (Career) { DrawSeasonEnd(*Career); } break;
	case EVC26Screen::Story: if (Career) { DrawStory(*Arena, *Career); } break;
	case EVC26Screen::Transfers: if (Career) { DrawTransfers(*Arena, *Career); } break;
	case EVC26Screen::Record: if (Career) { DrawRecord(*Arena, *Career); } break;
	}
}
