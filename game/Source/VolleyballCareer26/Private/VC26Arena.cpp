#include "VC26Arena.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogVC26, Log, All);

namespace
{
	constexpr float SimStep = 1.f / 120.f;
	constexpr float CpuSkillLevels[] = {0.35f, 0.7f, 1.f};
	constexpr float SlowMoScale = 0.4f;  // GDD 1.1: 0.4x for 300 ms on a power spike
	constexpr float SlowMoSeconds = 0.3f;

	FString SavePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("VC26Career.sav");
	}

	const TCHAR* ReasonText(vc26::PointReason Reason)
	{
		switch (Reason)
		{
		case vc26::PointReason::Ace: return TEXT("ACE!");
		case vc26::PointReason::Kill: return TEXT("KILL!");
		case vc26::PointReason::BallDown: return TEXT("BALL DOWN");
		case vc26::PointReason::Out: return TEXT("OUT");
		case vc26::PointReason::Antenna: return TEXT("OUTSIDE THE ANTENNA");
		case vc26::PointReason::Block: return TEXT("STUFF BLOCK!");
		case vc26::PointReason::NetTouch: return TEXT("NET TOUCH");
		}
		return TEXT("");
	}

	const TCHAR* QualityText(vc26::ContactQuality Quality)
	{
		switch (Quality)
		{
		case vc26::ContactQuality::Perfect: return TEXT("PERFECT");
		case vc26::ContactQuality::Good: return TEXT("GOOD");
		case vc26::ContactQuality::Shank: return TEXT("SHANK");
		}
		return TEXT("");
	}
}

AVC26Arena::AVC26Arena()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetFieldOfView(62.f);  // a little tighter than GDD 1.1, for the fixed broadcast view
	FPostProcessSettings& Look = Camera->PostProcessSettings;
	Look.bOverride_BloomIntensity = true;
	Look.BloomIntensity = 0.7f;
	Look.bOverride_VignetteIntensity = true;
	Look.VignetteIntensity = 0.35f;
	Look.bOverride_AmbientOcclusionIntensity = true;
	Look.AmbientOcclusionIntensity = 0.7f;
}

void AVC26Arena::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	BuildHall();
	BuildCourt();
	BuildCrowd();
	BuildPlayers();
	BuildBall();
	BuildLights();
	Sounds.Init();

	// -VC26Touch shows the phone controls on PC, with the mouse as one finger.
	bUseTouch = PLATFORM_ANDROID || PLATFORM_IOS || FParse::Param(FCommandLine::Get(), TEXT("VC26Touch"));

	// -VC26ShotDir=<folder> walks through every screen, saves a picture of each
	// and quits. It never touches the real save.
	FParse::Value(FCommandLine::Get(), TEXT("VC26ShotDir="), ShotDir);
	bHasSave = ShotDir.IsEmpty() && FPaths::FileExists(SavePath());

	if (FParse::Param(FCommandLine::Get(), TEXT("VC26Autoplay")))
	{
		GoTo(EVC26Screen::Match);
	}
	StartBackgroundMatch();
}

void AVC26Arena::StartBackgroundMatch()
{
	vc26::RallySim::Config Config;
	Config.seed = static_cast<std::uint32_t>(FDateTime::Now().GetTicks());
	Config.humanHome = false;
	Sim = MakeUnique<vc26::RallySim>(Config);
	bCareerMatch = false;
	HomeName = TEXT("HOME");
	AwayName = TEXT("AWAY");
	Accumulator = 0.f;
}

void AVC26Arena::StartMatch(bool bCareer)
{
	vc26::RallySim::Config Config;
	Config.seed = static_cast<std::uint32_t>(FDateTime::Now().GetTicks());
	Config.humanHome = true;
	Config.cpuSkill = CpuSkillLevels[Difficulty];
	HomeName = TEXT("HOME");
	AwayName = TEXT("AWAY");

	if (bCareer && Career.IsSet())
	{
		Config.rules = vc26::Career::matchRules();
		Config.cpuSkill = Career->opponentCpuSkill();
		Config.humanSkills = Career->skills();
		HomeName = UTF8_TO_TCHAR(Career->myTeam().name.c_str());
		AwayName = UTF8_TO_TCHAR(Career->teams()[Career->nextOpponent()].name.c_str());
	}

	Sim = MakeUnique<vc26::RallySim>(Config);
	Stats = {};
	bCareerMatch = bCareer;
	Accumulator = 0.f;
	bCameraReady = false;
	GoTo(EVC26Screen::Match);
}

void AVC26Arena::FinishMatch()
{
	if (!bCareerMatch || !Career.IsSet())
	{
		GoTo(EVC26Screen::MainMenu);
		StartBackgroundMatch();
		return;
	}

	const vc26::MatchScore& Score = Sim->score();
	Report = Career->recordMatch(Score.sets(vc26::Team::Home), Score.sets(vc26::Team::Away), Stats);
	SaveCareer();
	GoTo(EVC26Screen::Report);
	StartBackgroundMatch();
}

void AVC26Arena::LeaveMatch()
{
	// Leaving a career match doesn't count it, so it can be played again.
	GoTo(bCareerMatch ? EVC26Screen::Hub : EVC26Screen::MainMenu);
	StartBackgroundMatch();
}

void AVC26Arena::GoTo(EVC26Screen NewScreen)
{
	Screen = NewScreen;
	MenuIndex = 0;
}

TArray<FVC26MenuItem> AVC26Arena::GetMenuItems() const
{
	TArray<FVC26MenuItem> Items;
	switch (Screen)
	{
	case EVC26Screen::MainMenu:
		if (bHasSave)
		{
			Items.Add({TEXT("Continue Career"), EVC26MenuAction::ContinueCareer});
		}
		Items.Add({TEXT("New Career"), EVC26MenuAction::NewCareer});
		Items.Add({TEXT("Quick Match"), EVC26MenuAction::QuickMatch});
		Items.Add({TEXT("Quit"), EVC26MenuAction::Quit});
		break;
	case EVC26Screen::Hub:
		Items.Add({Career.IsSet() && Career->stage() == vc26::CareerStage::Final ? TEXT("Play the Final") : TEXT("Play Next Match"), EVC26MenuAction::PlayNext});
		Items.Add({TEXT("Upgrade Skills"), EVC26MenuAction::OpenUpgrades});
		Items.Add({TEXT("Main Menu"), EVC26MenuAction::ToMainMenu});
		break;
	case EVC26Screen::Upgrades:
		for (int32 i = 0; i < vc26::kSkills; ++i)
		{
			Items.Add({UTF8_TO_TCHAR(vc26::toString(static_cast<vc26::Skill>(i))), EVC26MenuAction::Upgrade, i});
		}
		Items.Add({TEXT("Back"), EVC26MenuAction::Back});
		break;
	default:
		break;
	}
	return Items;
}

void AVC26Arena::ConfirmMenu()
{
	// These screens have no list: Enter just moves on.
	switch (Screen)
	{
	case EVC26Screen::CreatePlayer:
		// Keeps the previous career, in case this one was started by mistake.
		if (bHasSave)
		{
			IFileManager::Get().Copy(*(SavePath() + TEXT(".bak")), *SavePath());
		}
		Career.Emplace(std::string("The Rookie"), GetChosenArchetype(), static_cast<std::uint32_t>(FDateTime::Now().GetTicks()));
		SaveCareer();
		GoTo(EVC26Screen::Hub);
		return;
	case EVC26Screen::Report:
		GoTo(Career->stage() == vc26::CareerStage::SeasonOver ? EVC26Screen::SeasonEnd : EVC26Screen::Hub);
		return;
	case EVC26Screen::SeasonEnd:
		Career->startNextSeason();
		SaveCareer();
		GoTo(EVC26Screen::Hub);
		return;
	default:
		break;
	}

	const TArray<FVC26MenuItem> Items = GetMenuItems();
	if (!Items.IsValidIndex(MenuIndex))
	{
		return;
	}
	const FVC26MenuItem& Item = Items[MenuIndex];
	switch (Item.Action)
	{
	case EVC26MenuAction::ContinueCareer:
		if (LoadCareer())
		{
			GoTo(Career->stage() == vc26::CareerStage::SeasonOver ? EVC26Screen::SeasonEnd : EVC26Screen::Hub);
		}
		break;
	case EVC26MenuAction::NewCareer: GoTo(EVC26Screen::CreatePlayer); break;
	case EVC26MenuAction::QuickMatch: StartMatch(false); break;
	case EVC26MenuAction::Quit: FPlatformMisc::RequestExit(false, TEXT("VC26 menu")); break;
	case EVC26MenuAction::PlayNext: StartMatch(true); break;
	case EVC26MenuAction::OpenUpgrades: GoTo(EVC26Screen::Upgrades); break;
	case EVC26MenuAction::ToMainMenu: GoTo(EVC26Screen::MainMenu); break;
	case EVC26MenuAction::Upgrade:
		if (Career->upgrade(static_cast<vc26::Skill>(Item.Arg)))
		{
			SaveCareer();
		}
		break;
	case EVC26MenuAction::Back: GoTo(EVC26Screen::Hub); break;
	}
}

void AVC26Arena::UpdateMenus()
{
	const APlayerController* Player = Cast<APlayerController>(GetController());
	if (!Player)
	{
		return;
	}
	auto Pressed = [Player](std::initializer_list<FKey> Keys)
	{
		for (const FKey& Key : Keys)
		{
			if (Player->WasInputKeyJustPressed(Key))
			{
				return true;
			}
		}
		return false;
	};

	const int32 Count = GetMenuItems().Num();
	if (Count > 0 && Pressed({EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up}))
	{
		MenuIndex = (MenuIndex + Count - 1) % Count;
	}
	if (Count > 0 && Pressed({EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down}))
	{
		MenuIndex = (MenuIndex + 1) % Count;
	}
	if (Screen == EVC26Screen::CreatePlayer)
	{
		if (Pressed({EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left}))
		{
			ChosenArchetype = (ChosenArchetype + vc26::kArchetypes - 1) % vc26::kArchetypes;
		}
		if (Pressed({EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right}))
		{
			ChosenArchetype = (ChosenArchetype + 1) % vc26::kArchetypes;
		}
	}
	if (Pressed({EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom}))
	{
		ConfirmMenu();
	}
	else if (Pressed({EKeys::Escape, EKeys::Gamepad_FaceButton_Right}))
	{
		if (Screen == EVC26Screen::Upgrades)
		{
			GoTo(EVC26Screen::Hub);
		}
		else if (Screen == EVC26Screen::CreatePlayer)
		{
			GoTo(EVC26Screen::MainMenu);
		}
	}
}

void AVC26Arena::SaveCareer()
{
	if (Career.IsSet() && ShotDir.IsEmpty())
	{
		FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(Career->save().c_str())), *SavePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		bHasSave = true;
	}
}

bool AVC26Arena::LoadCareer()
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *SavePath()))
	{
		return false;
	}
	std::optional<vc26::Career> Loaded = vc26::Career::load(TCHAR_TO_UTF8(*Text));
	if (!Loaded)
	{
		UE_LOG(LogVC26, Warning, TEXT("The career save could not be read"));
		bHasSave = false;  // takes Continue off the menu; the file stays for recovery
		return false;
	}
	Career.Emplace(*Loaded);
	return true;
}

const TCHAR* AVC26Arena::GetDifficultyName() const
{
	return Difficulty == 0 ? TEXT("Easy") : Difficulty == 1 ? TEXT("Normal") : TEXT("Hard");
}

void AVC26Arena::SetDifficulty(int32 Level)
{
	Difficulty = Level;
	Sim->setCpuSkill(CpuSkillLevels[Level]);
	SetBanner(FString::Printf(TEXT("DIFFICULTY: %s"), GetDifficultyName()).ToUpper());
}

void AVC26Arena::UpdateTouch()
{
	const APlayerController* Player = Cast<APlayerController>(GetController());
	if (!bUseTouch || !Player)
	{
		return;
	}

	TArray<vc26::TouchPoint, TInlineAllocator<11>> Fingers;
	for (int32 Finger = 0; Finger < 10; ++Finger)
	{
		float X = 0.f;
		float Y = 0.f;
		bool bDown = false;
		Player->GetInputTouchState(static_cast<ETouchIndex::Type>(Finger), X, Y, bDown);
		if (bDown)
		{
			Fingers.Add({Finger, X, Y});
		}
	}

	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PLATFORM_ANDROID && Player->IsInputKeyDown(EKeys::LeftMouseButton) && Player->GetMousePosition(MouseX, MouseY))
	{
		Fingers.Add({100, MouseX, MouseY});
	}

	int32 Width = 0;
	int32 Height = 0;
	Player->GetViewportSize(Width, Height);
	Touch.update(Fingers.GetData(), Fingers.Num(), static_cast<float>(Width), static_cast<float>(Height));
	if (Touch.difficultyTapped() && !bCareerMatch)
	{
		SetDifficulty((Difficulty + 1) % 3);
	}
}

vc26::SimInput AVC26Arena::ReadInput() const
{
	vc26::SimInput Input;
	const APlayerController* Player = Cast<APlayerController>(GetController());
	if (!Player)
	{
		return Input;
	}

	auto Held = [Player](const FKey& A, const FKey& B) { return Player->IsInputKeyDown(A) || Player->IsInputKeyDown(B); };

	Input.moveX = (Held(EKeys::W, EKeys::Up) ? 1.f : 0.f) - (Held(EKeys::S, EKeys::Down) ? 1.f : 0.f);
	Input.moveY = (Held(EKeys::A, EKeys::Left) ? 1.f : 0.f) - (Held(EKeys::D, EKeys::Right) ? 1.f : 0.f);
	if (bUseTouch)
	{
		const vc26::SimInput Fingers = Touch.input();
		Input.moveX += Fingers.moveX;
		Input.moveY += Fingers.moveY;
	}
	Input.moveX += Player->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	Input.moveY -= Player->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	Input.moveX = FMath::Clamp(Input.moveX, -1.f, 1.f);
	Input.moveY = FMath::Clamp(Input.moveY, -1.f, 1.f);
	Input.action = Held(EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom) || (bUseTouch && Touch.actionHeld());
	return Input;
}

void AVC26Arena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Sim)
	{
		return;
	}

	UpdateTouch();
	const bool bInMatch = Screen == EVC26Screen::Match;
	const APlayerController* Player = Cast<APlayerController>(GetController());

	if (!bInMatch)
	{
		UpdateMenus();
	}
	else if (Player)
	{
		if (Player->WasInputKeyJustPressed(EKeys::Escape))
		{
			// A finished match always counts, so a loss can't be walked away from.
			if (Sim->phase() == vc26::RallyPhase::MatchOver)
			{
				FinishMatch();
			}
			else
			{
				LeaveMatch();
			}
		}
		else if (Sim->humanHome() && !bCareerMatch)
		{
			const FKey LevelKeys[] = {EKeys::One, EKeys::Two, EKeys::Three};
			for (int32 Level = 0; Level < 3; ++Level)
			{
				if (Player->WasInputKeyJustPressed(LevelKeys[Level]))
				{
					SetDifficulty(Level);
				}
			}
		}
	}

	vc26::SimInput Input = Screen == EVC26Screen::Match ? ReadInput() : vc26::SimInput();
	if (!ShotDir.IsEmpty() && Sim->humanServing())
	{
		Input.action = FMath::Fmod(TourTime, 1.5f) < 0.6f;  // the tour serves for itself
	}
	if (Sim->phase() == vc26::RallyPhase::MatchOver)
	{
		if (Screen != EVC26Screen::Match || !Sim->humanHome())
		{
			StartBackgroundMatch();  // the match behind the menus just keeps going
		}
		else if (Player && (Player->WasInputKeyJustPressed(EKeys::SpaceBar) || Player->WasInputKeyJustPressed(EKeys::Enter) ||
			Player->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) || (bUseTouch && Touch.actionHeld() && !bTouchWasHeld)))
		{
			FinishMatch();
		}
	}
	bTouchWasHeld = bUseTouch && Touch.actionHeld();

	// Fixed steps keep the timing windows and results independent of frame rate.
	const float SimDelta = SlowMoLeft > 0.f ? DeltaSeconds * SlowMoScale : DeltaSeconds;
	Accumulator = FMath::Min(Accumulator + SimDelta, 0.25f);
	while (Accumulator >= SimStep)
	{
		Sim->step(SimStep, Input);
		Accumulator -= SimStep;
	}

	SlowMoLeft = FMath::Max(SlowMoLeft - DeltaSeconds, 0.f);
	Sounds.SetMuted(Screen != EVC26Screen::Match);  // the match behind the menus stays silent
	HandleEvents();
	SyncShapes(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
	Sounds.Tick(DeltaSeconds);
	BannerTimeLeft = FMath::Max(BannerTimeLeft - DeltaSeconds, 0.f);

	if (!ShotDir.IsEmpty())
	{
		RunScreenshotTour(DeltaSeconds);
	}
}

void AVC26Arena::SetBanner(const FString& Text)
{
	Banner = Text;
	BannerTimeLeft = 1.6f;
}

void AVC26Arena::HandleEvents()
{
	for (const vc26::SimEvent& Event : Sim->takeEvents())
	{
		vc26::recordEvent(Stats, Event);

		switch (Event.type)
		{
		case vc26::SimEvent::Type::Contact:
		{
			EVC26Sound Sound = EVC26Sound::Set;
			float Volume = 1.f;
			float Pitch = 1.f;
			EPose Swing = EPose::Set;
			switch (Event.kind)
			{
			case vc26::ContactKind::Serve:
				Sound = EVC26Sound::Spike;
				Volume = 0.6f;
				Pitch = 1.2f;
				Swing = EPose::AttackSwing;
				break;
			case vc26::ContactKind::Pass:
				Sound = EVC26Sound::Pass;
				Swing = EPose::Pass;
				break;
			case vc26::ContactKind::Set:
			case vc26::ContactKind::FreeBall:
				break;
			case vc26::ContactKind::Attack:
				Sound = EVC26Sound::Spike;
				Volume = Event.quality == vc26::ContactQuality::Shank ? 0.5f : 1.f;
				Swing = EPose::AttackSwing;
				ShakeLeft = Event.quality == vc26::ContactQuality::Perfect ? 0.25f : 0.12f;
				if (Event.quality == vc26::ContactQuality::Perfect && Event.team == vc26::Team::Home && Sim->humanHome())
				{
					SlowMoLeft = SlowMoSeconds;
				}
				break;
			case vc26::ContactKind::Block:
				Sound = EVC26Sound::Block;
				Swing = EPose::Block;
				break;
			}
			FFigure& Figure = Figures[Event.player];
			Figure.Swing = Swing;
			Figure.SwingLeft = 0.28f;
			if (Event.stretched)
			{
				Figure.Swing = EPose::Dive;
				Figure.SwingLeft = 0.75f;
				Figure.DiveDirection = (SimToWorld(Event.where) - SimToWorld(Sim->players()[Event.player].pos)).GetSafeNormal2D();
			}
			Sounds.Play(this, Sound, Volume, Pitch);

			if (Event.kind == vc26::ContactKind::Block)
			{
				SetBanner(Event.quality == vc26::ContactQuality::Perfect ? TEXT("ROOF BLOCK!") : TEXT("BLOCK TOUCH"));
			}
			else if (Event.team == vc26::Team::Home && Event.kind != vc26::ContactKind::Serve && Sim->humanHome())
			{
				SetBanner(QualityText(Event.quality));
			}
			break;
		}
		case vc26::SimEvent::Type::Whiff:
			if (Event.team == vc26::Team::Home && Sim->humanHome())
			{
				SetBanner(TEXT("TOO FAR AWAY"));
			}
			break;
		case vc26::SimEvent::Type::NetHit:
			Sounds.Play(this, EVC26Sound::Net);
			SetBanner(TEXT("NET"));
			break;
		case vc26::SimEvent::Type::Point:
		{
			CrowdExcitement = FMath::Min(0.5f + Sim->contactsThisRally() / 12.f, 1.f);
			if (Sim->ball().pos.z < 0.5f)
			{
				Sounds.Play(this, EVC26Sound::Floor);
			}
			Sounds.Play(this, EVC26Sound::Whistle, 0.35f);
			// Long rallies get the bigger reaction (GDD 4).
			Sounds.Play(this, EVC26Sound::Cheer, 0.25f + 0.5f * FMath::Min(Sim->contactsThisRally() / 12.f, 1.f));
			SetBanner(FString::Printf(TEXT("%s  %s POINT"), ReasonText(Event.reason), Event.team == vc26::Team::Home ? *HomeName.ToUpper() : *AwayName.ToUpper()));

			const vc26::MatchScore& Score = Sim->score();
			UE_LOG(LogVC26, Log, TEXT("Point: %s %s | contacts %d | set %d | %d-%d"), Event.team == vc26::Team::Home ? TEXT("Home") : TEXT("Away"),
				ReasonText(Event.reason), Sim->contactsThisRally(), Score.setNumber(), Score.points(vc26::Team::Home), Score.points(vc26::Team::Away));
			break;
		}
		}
	}
}

void AVC26Arena::RunScreenshotTour(float DeltaSeconds)
{
	TourTime += DeltaSeconds;
	auto Shot = [this](const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(ShotDir / FString(Name) + TEXT(".png"), true, false);
		UE_LOG(LogVC26, Log, TEXT("Tour shot: %s"), Name);
	};

	// The last steps wait for a moment in a live match, with a time limit.
	if (TourStep == 16 || TourStep == 17)
	{
		const vc26::PendingContact& Pending = Sim->pending();
		const bool bRing = TourStep == 16 && Pending.active && Pending.team == vc26::Team::Home && Pending.timeToContact < 0.5f;
		const bool bBlock = TourStep == 17 && Sim->humanBlocking() && Sim->block().timeToContact < 0.6f;
		if (bRing || bBlock || TourTime > 40.f)
		{
			Shot(TourStep == 16 ? TEXT("10_rings") : TEXT("11_block"));
			++TourStep;
			TourTime = 0.f;
		}
		return;
	}

	if (TourTime < 3.f)
	{
		return;
	}
	TourTime = 0.f;

	auto DemoStats = []()
	{
		vc26::MatchStats Demo;
		Demo.perfect = 22;
		Demo.good = 14;
		Demo.shanks = 4;
		Demo.kills = 9;
		Demo.aces = 2;
		Demo.blocks = 1;
		Demo.errors = 3;
		Demo.clutchPoints = 2;
		Demo.pointsWon = 30;
		Demo.pointsLost = 20;
		return Demo;
	};

	switch (TourStep++)
	{
	case 0: break;  // lets the first frames settle
	case 1: Shot(TEXT("1_menu")); break;
	case 2: GoTo(EVC26Screen::CreatePlayer); break;
	case 3: Shot(TEXT("2_create")); break;
	case 4:
		Career.Emplace(std::string("The Rookie"), vc26::Archetype::PowerSpiker, 7u);
		for (int32 i = 0; i < 3; ++i)
		{
			Career->recordMatch(2, i, DemoStats());
		}
		GoTo(EVC26Screen::Hub);
		break;
	case 5: Shot(TEXT("3_hub")); break;
	case 6: GoTo(EVC26Screen::Upgrades); break;
	case 7: Shot(TEXT("4_upgrades")); break;
	case 8:
		Stats = DemoStats();
		Report = Career->recordMatch(2, 1, Stats);
		GoTo(EVC26Screen::Report);
		break;
	case 9: Shot(TEXT("5_report")); break;
	case 10:
		while (Career->stage() != vc26::CareerStage::SeasonOver)
		{
			Career->recordMatch(2, 0, DemoStats());
		}
		GoTo(EVC26Screen::SeasonEnd);
		break;
	case 11: Shot(TEXT("6_season")); break;
	case 12: StartBackgroundMatch(); GoTo(EVC26Screen::Match); break;
	case 13: Shot(TEXT("7_match_a")); break;
	case 14: Shot(TEXT("8_match_b")); break;
	case 15: Shot(TEXT("9_match_c")); StartMatch(false); break;
	default: FPlatformMisc::RequestExit(false, TEXT("VC26 screenshot tour")); break;
	}
}
