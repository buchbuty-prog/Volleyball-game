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
	constexpr float ReplaySpeed = 0.45f;

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
	FParse::Value(FCommandLine::Get(), TEXT("VC26TourStart="), TourStep);
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
	ApplyVenue(false);
	bCouch = false;
	ReplayCountdown = -1.f;
	bCareerMatch = false;
	HomeName = TEXT("HOME");
	AwayName = TEXT("AWAY");
	SetRosters();
	Accumulator = 0.f;
}

void AVC26Arena::SetRosters()
{
	HomeRoster = vc26::makeRoster(TCHAR_TO_UTF8(*HomeName));
	AwayRoster = vc26::makeRoster(TCHAR_TO_UTF8(*AwayName));
	if (bCareerMatch && Career.IsSet())
	{
		HomeRoster[Sim->players()[Career->lockedPlayer()].id - 1] = Career->name();
	}
	PointRun.reset();
	LastTouch[0] = LastTouch[1] = -1;
	Commentary.Reset();
	CommentaryTimeLeft = 0.f;
	for (FFigure& Figure : Figures)
	{
		Figure.ShownId = -1;  // new people in these bodies
	}
}

vc26::Appearance AVC26Arena::LookOf(int32 Player) const
{
	const vc26::SimPlayer& Body = Sim->players()[Player];
	if (bCareerMatch && Career.IsSet() && Player == Career->lockedPlayer())
	{
		return Career->look();
	}
	return vc26::makeTeamAppearance(TCHAR_TO_UTF8(*(Body.team == vc26::Team::Home ? HomeName : AwayName)), Body.id);
}

vc26::Position AVC26Arena::GetDraftPosition() const
{
	if (bRestyling && Career.IsSet())
	{
		return Career->position();
	}
	// The same bodies Career::lockedPlayer gives each kind of player.
	switch (GetChosenArchetype())
	{
	case vc26::Archetype::PrecisionSetter: return vc26::Position::Setter;
	case vc26::Archetype::BlockSpecialist: return vc26::Position::Opposite;
	default: return vc26::Position::Outside;
	}
}

void AVC26Arena::OpenLook(bool bRestyle)
{
	bRestyling = bRestyle;
	DraftFace = 0;
	if (!bRestyle)
	{
		DraftLook = vc26::makeAppearance(GetDraftPosition(), static_cast<std::uint32_t>(FDateTime::Now().GetTicks()));
	}
	else
	{
		DraftLook = Career->look();
	}
	GoTo(EVC26Screen::Look);
	GalleryPose = static_cast<int32>(EPose::Idle);
	GalleryPlayer = 0;
	ShowLookPreview();
}

void AVC26Arena::CloseLook()
{
	GalleryPose = -1;
	Figures[GalleryPlayer].ShownId = -1;  // back to whoever this body really is
}

void AVC26Arena::ShowLookPreview()
{
	if (!Athletes.IsEmpty())
	{
		ApplyLook(GalleryPlayer, DraftLook);
	}
	// Close on the face for the face, and back for the body and kit.
	const TArray<FVC26MenuItem> Items = GetMenuItems();
	const bool bRow = Items.IsValidIndex(MenuIndex) && Items[MenuIndex].Action == EVC26MenuAction::LookRow;
	const ELookRow Row = bRow ? static_cast<ELookRow>(Items[MenuIndex].Arg) : ELookRow::Height;
	const bool bFace = Row >= ELookRow::Face && Row <= ELookRow::Headband;
	const float Tall = static_cast<float>(DraftLook.heightCm);
	GalleryAim = bFace ? Tall * 0.93f : Tall * 0.52f;
	GalleryView = bFace ? FVector(95.f, 40.f, Tall * 0.93f) : FVector(340.f, 130.f, Tall * 0.55f);
}

FString AVC26Arena::LookRowText(ELookRow Row) const
{
	static const TCHAR* const Colours[] = {TEXT("Black"), TEXT("Dark Brown"), TEXT("Brown"), TEXT("Auburn"), TEXT("Blond"), TEXT("Grey"), TEXT("Dyed Red")};
	static const TCHAR* const Bands[] = {TEXT("None"), TEXT("Black"), TEXT("White"), TEXT("Team Colour")};
	static const TCHAR* const Shoes[] = {TEXT("White"), TEXT("Black"), TEXT("Volt"), TEXT("Red"), TEXT("Royal"), TEXT("Sunset")};
	static const TCHAR* const Sleeves[] = {TEXT("None"), TEXT("Hitting Arm"), TEXT("Other Arm"), TEXT("Both")};
	static const TCHAR* const Tattoos[] = {TEXT("None"), TEXT("Bands"), TEXT("Forearm")};
	const vc26::Appearance& Look = DraftLook;
	auto Shape = [&Look](vc26::FaceShape Part) { return FString::Printf(TEXT("%+d"), Look.face[static_cast<int32>(Part)]); };
	switch (Row)
	{
	case ELookRow::Height: return FString::Printf(TEXT("%d cm"), Look.heightCm);
	case ELookRow::Mass: return FString::Printf(TEXT("%d kg"), Look.massKg);
	case ELookRow::Arms: return FString::Printf(TEXT("%+d"), Look.arms);
	case ELookRow::Face: return FString::Printf(TEXT("%d of %d"), DraftFace + 1, vc26::kFacePresets);
	case ELookRow::Nose: return Shape(vc26::FaceShape::Nose);
	case ELookRow::Jaw: return Shape(vc26::FaceShape::Jaw);
	case ELookRow::Chin: return Shape(vc26::FaceShape::Chin);
	case ELookRow::Cheeks: return Shape(vc26::FaceShape::Cheeks);
	case ELookRow::Brow: return Shape(vc26::FaceShape::Brow);
	case ELookRow::Forehead: return Shape(vc26::FaceShape::Forehead);
	case ELookRow::Lips: return Shape(vc26::FaceShape::Lips);
	case ELookRow::Skin: return FString::Printf(TEXT("%d of %d"), Look.skin + 1, vc26::kSkinTones);
	case ELookRow::Hair: return UTF8_TO_TCHAR(vc26::toString(Look.hair));
	case ELookRow::HairColour: return Colours[Look.hairColour];
	case ELookRow::FacialHair: return UTF8_TO_TCHAR(vc26::toString(Look.facialHair));
	case ELookRow::Headband: return Bands[Look.headband];
	case ELookRow::Shoes: return Shoes[Look.shoes];
	case ELookRow::Knees: return Bands[Look.kneeSleeve];
	case ELookRow::ArmSleeve: return Sleeves[Look.armSleeve];
	case ELookRow::Socks: return Look.highSocks ? TEXT("High") : TEXT("Low");
	case ELookRow::Tattoo: return Tattoos[Look.tattoo];
	case ELookRow::Tape: return Look.fingerTape ? TEXT("Taped") : TEXT("None");
	default: return FString();
	}
}

void AVC26Arena::ChangeLookRow(ELookRow Row, int32 Step)
{
	vc26::Appearance& Look = DraftLook;
	auto Wrap = [Step](int32& Value, int32 Count) { Value = (Value + Step + Count) % Count; };
	auto Slide = [Step](int32& Value, int32 Low, int32 High) { Value = FMath::Clamp(Value + Step, Low, High); };
	auto Part = [&Look, &Slide](vc26::FaceShape Shape) { Slide(Look.face[static_cast<int32>(Shape)], -vc26::kFaceRange, vc26::kFaceRange); };
	switch (Row)
	{
	case ELookRow::Height: Slide(Look.heightCm, vc26::kMinHeight, vc26::kMaxHeight); break;
	case ELookRow::Mass: Slide(Look.massKg, vc26::kMinMass, vc26::kMaxMass); break;
	case ELookRow::Arms: Slide(Look.arms, -vc26::kArmRange, vc26::kArmRange); break;
	case ELookRow::Face:
		Wrap(DraftFace, vc26::kFacePresets);
		Look.face = vc26::facePreset(DraftFace);
		break;
	case ELookRow::Nose: Part(vc26::FaceShape::Nose); break;
	case ELookRow::Jaw: Part(vc26::FaceShape::Jaw); break;
	case ELookRow::Chin: Part(vc26::FaceShape::Chin); break;
	case ELookRow::Cheeks: Part(vc26::FaceShape::Cheeks); break;
	case ELookRow::Brow: Part(vc26::FaceShape::Brow); break;
	case ELookRow::Forehead: Part(vc26::FaceShape::Forehead); break;
	case ELookRow::Lips: Part(vc26::FaceShape::Lips); break;
	case ELookRow::Skin: Wrap(Look.skin, vc26::kSkinTones); break;
	case ELookRow::Hair:
	{
		int32 Style = static_cast<int32>(Look.hair);
		Wrap(Style, vc26::kHairStyles);
		Look.hair = static_cast<vc26::HairStyle>(Style);
		break;
	}
	case ELookRow::HairColour: Wrap(Look.hairColour, vc26::kHairColours); break;
	case ELookRow::FacialHair:
	{
		int32 Style = static_cast<int32>(Look.facialHair);
		Wrap(Style, vc26::kFacialHairs);
		Look.facialHair = static_cast<vc26::FacialHair>(Style);
		break;
	}
	case ELookRow::Headband: Wrap(Look.headband, vc26::kHeadbands); break;
	case ELookRow::Shoes: Wrap(Look.shoes, vc26::kShoes); break;
	case ELookRow::Knees: Wrap(Look.kneeSleeve, vc26::kKneeSleeves); break;
	case ELookRow::ArmSleeve: Wrap(Look.armSleeve, vc26::kArmSleeves); break;
	case ELookRow::Socks: Look.highSocks = !Look.highSocks; break;
	case ELookRow::Tattoo: Wrap(Look.tattoo, vc26::kTattoos); break;
	case ELookRow::Tape: Look.fingerTape = !Look.fingerTape; break;
	default: break;
	}
}

FString AVC26Arena::NameOf(vc26::Team Team, int32 Player) const
{
	if (Player < 0)
	{
		return FString();
	}
	const vc26::Roster& Roster = Team == vc26::Team::Home ? HomeRoster : AwayRoster;
	return UTF8_TO_TCHAR(Roster[FMath::Clamp(Sim->players()[Player].id - 1, 0, vc26::kRosterSize - 1)].c_str());
}

FString AVC26Arena::PlayerLabel(int32 Player) const
{
	const vc26::SimPlayer& Body = Sim->players()[Player];
	return FString::Printf(TEXT("#%d %s"), Body.id, *NameOf(Body.team, Player));
}

void AVC26Arena::CallPoint(const vc26::SimEvent& Event)
{
	const vc26::Team Loser = Event.team == vc26::Team::Home ? vc26::Team::Away : vc26::Team::Home;
	vc26::PointCall Call;
	Call.reason = Event.reason;
	Call.scorer = TCHAR_TO_UTF8(*NameOf(Event.team, LastTouch[static_cast<int32>(Event.team)]));
	Call.blunder = TCHAR_TO_UTF8(*NameOf(Loser, LastTouch[static_cast<int32>(Loser)]));
	Call.team = TCHAR_TO_UTF8(*(Event.team == vc26::Team::Home ? HomeName : AwayName));
	Call.contacts = Sim->contactsThisRally();
	Call.run = PointRun.add(Event.team);
	Call.setOnTheLine = Event.clutch;
	Commentary = UTF8_TO_TCHAR(vc26::callPoint(Call, CallCount++).c_str());
	CommentaryTimeLeft = 4.f;
	LastTouch[0] = LastTouch[1] = -1;
}

void AVC26Arena::StartMatch(bool bCareer, EVC26Mode Mode)
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
		Config.lockedPlayer = Career->lockedPlayer();
		Config.teammateSkill = Career->teammateSkill();
		HomeName = UTF8_TO_TCHAR(Career->myTeam().name.c_str());
		AwayName = UTF8_TO_TCHAR(Career->teams()[Career->nextOpponent()].name.c_str());
	}

	if (Mode == EVC26Mode::Beach)
	{
		Config.rules = vc26::MatchRules::beach();
		Config.beach = true;
	}
	if (Mode == EVC26Mode::Couch)
	{
		Config.humanAway = true;
		HomeName = TEXT("PLAYER 1");
		AwayName = TEXT("PLAYER 2");
	}
	ApplyVenue(Mode == EVC26Mode::Beach);
	bCouch = Mode == EVC26Mode::Couch;

	Sim = MakeUnique<vc26::RallySim>(Config);
	bCareerMatch = bCareer;
	SetRosters();
	Stats = {};
	LockedPlayer = Config.lockedPlayer;
	Highlights.Reset();
	Snapshot.Reset();
	InputLog.Reset();
	ReplayCountdown = -1.f;
	Accumulator = 0.f;
	bCameraReady = false;
	GoTo(EVC26Screen::Match);
}

void AVC26Arena::FinishMatch()
{
	if (bCouch)
	{
		// Two people played, so one side's report would tell the winner they lost.
		GoTo(EVC26Screen::MainMenu);
		StartBackgroundMatch();
		return;
	}

	const vc26::MatchScore& Score = Sim->score();
	const int32 SetsFor = Score.sets(vc26::Team::Home);
	const int32 SetsAgainst = Score.sets(vc26::Team::Away);
	if (bCareerMatch && Career.IsSet())
	{
		Report = Career->recordMatch(SetsFor, SetsAgainst, Stats);
		SaveCareer();
	}
	else
	{
		// A friendly gets the same report, with nothing to earn.
		Report = {};
		Report.won = SetsFor > SetsAgainst;
		Report.setsFor = SetsFor;
		Report.setsAgainst = SetsAgainst;
		Report.rating = vc26::matchRating(Stats, Report.won);
		Report.stats = Stats;
	}
	bReportFromCareer = bCareerMatch;
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
		Items.Add({TEXT("Beach 2v2"), EVC26MenuAction::BeachMatch});
		Items.Add({TEXT("Couch Match (2 Players)"), EVC26MenuAction::CouchMatch});
		Items.Add({TEXT("Quit"), EVC26MenuAction::Quit});
		break;
	case EVC26Screen::Hub:
		if (Career.IsSet() && !Career->canPlay())
		{
			Items.Add({Career->injuredMatches() > 0 ? TEXT("Watch From The Stands") : TEXT("Watch From The Bench"), EVC26MenuAction::SitOut});
		}
		else
		{
			Items.Add({Career.IsSet() && Career->stage() == vc26::CareerStage::Final ? TEXT("Play the Final") : TEXT("Play Next Match"), EVC26MenuAction::PlayNext});
		}
		Items.Add({TEXT("Upgrade Skills"), EVC26MenuAction::OpenUpgrades});
		Items.Add({TEXT("Career Record"), EVC26MenuAction::OpenRecord});
		Items.Add({TEXT("Change Your Look"), EVC26MenuAction::OpenLook});
		Items.Add({TEXT("Main Menu"), EVC26MenuAction::ToMainMenu});
		break;
	case EVC26Screen::Record:
		Items.Add({TEXT("Back"), EVC26MenuAction::Back});
		break;
	case EVC26Screen::Look:
	{
		static const TCHAR* const Names[] = {
			TEXT("Height"), TEXT("Weight"), TEXT("Arm Length"),
			TEXT("Face"), TEXT("Nose"), TEXT("Jaw"), TEXT("Chin"), TEXT("Cheeks"), TEXT("Brow"), TEXT("Forehead"), TEXT("Lips"),
			TEXT("Skin Tone"), TEXT("Hair"), TEXT("Hair Colour"), TEXT("Facial Hair"), TEXT("Headband"),
			TEXT("Shoes"), TEXT("Knee Sleeves"), TEXT("Arm Sleeve"), TEXT("Socks"), TEXT("Tattoo"), TEXT("Finger Tape")};
		for (int32 Row = bRestyling ? static_cast<int32>(ELookRow::Face) : 0; Row < static_cast<int32>(ELookRow::Count); ++Row)
		{
			Items.Add({Names[Row], EVC26MenuAction::LookRow, Row, LookRowText(static_cast<ELookRow>(Row))});
		}
		Items.Add({TEXT("Randomise"), EVC26MenuAction::LookRandom});
		Items.Add({bRestyling ? TEXT("Done") : TEXT("Start Career"), EVC26MenuAction::LookDone});
		break;
	}
	case EVC26Screen::Transfers:
		if (Career.IsSet())
		{
			const std::vector<vc26::TransferOffer>& Offers = Career->offers();
			for (int32 i = 0; i < static_cast<int32>(Offers.size()); ++i)
			{
				Items.Add({FString::Printf(TEXT("Sign for %s"), UTF8_TO_TCHAR(Offers[i].club.c_str())), EVC26MenuAction::SignOffer, i});
			}
			const FString Club = UTF8_TO_TCHAR(Career->nextClub().c_str());
			Items.Add({(Career->promoted() ? TEXT("Join ") : TEXT("Stay with ")) + Club, EVC26MenuAction::SignOffer, -1});
		}
		break;
	case EVC26Screen::Upgrades:
		for (int32 i = 0; i < vc26::kSkills; ++i)
		{
			Items.Add({UTF8_TO_TCHAR(vc26::toString(static_cast<vc26::Skill>(i))), EVC26MenuAction::Upgrade, i});
		}
		Items.Add({TEXT("Back"), EVC26MenuAction::Back});
		break;
	case EVC26Screen::Story:
		if (const vc26::StoryEvent* Story = Career.IsSet() ? Career->pendingStory() : nullptr)
		{
			Items.Add({UTF8_TO_TCHAR(Story->options[0]), EVC26MenuAction::StoryChoice, 0});
			Items.Add({UTF8_TO_TCHAR(Story->options[1]), EVC26MenuAction::StoryChoice, 1});
		}
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
		OpenLook(false);
		return;
	case EVC26Screen::Report:
		if (!bReportFromCareer)
		{
			GoTo(EVC26Screen::MainMenu);
			return;
		}
		GoTo(Career->pendingStory() ? EVC26Screen::Story : Career->stage() == vc26::CareerStage::SeasonOver ? EVC26Screen::SeasonEnd : EVC26Screen::Hub);
		return;
	case EVC26Screen::SeasonEnd:
		if (!Career->offers().empty())
		{
			GoTo(EVC26Screen::Transfers);
			return;
		}
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
			GoTo(Career->pendingStory() ? EVC26Screen::Story : Career->stage() == vc26::CareerStage::SeasonOver ? EVC26Screen::SeasonEnd : EVC26Screen::Hub);
		}
		break;
	case EVC26MenuAction::NewCareer: GoTo(EVC26Screen::CreatePlayer); break;
	case EVC26MenuAction::QuickMatch: StartMatch(false); break;
	case EVC26MenuAction::BeachMatch: StartMatch(false, EVC26Mode::Beach); break;
	case EVC26MenuAction::CouchMatch: StartMatch(false, EVC26Mode::Couch); break;
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
	case EVC26MenuAction::SitOut:
		Report = Career->sitOut();
		SaveCareer();
		bReportFromCareer = true;
		Highlights.Reset();
		GoTo(EVC26Screen::Report);
		break;
	case EVC26MenuAction::OpenRecord: GoTo(EVC26Screen::Record); break;
	case EVC26MenuAction::OpenLook: OpenLook(true); break;
	case EVC26MenuAction::LookRow: break;
	case EVC26MenuAction::LookRandom:
	{
		// A new player gets a whole new body, and a returning one only a new style.
		vc26::Appearance Fresh = vc26::makeAppearance(GetDraftPosition(), static_cast<std::uint32_t>(FDateTime::Now().GetTicks()));
		if (bRestyling)
		{
			Fresh.heightCm = DraftLook.heightCm;
			Fresh.massKg = DraftLook.massKg;
			Fresh.arms = DraftLook.arms;
		}
		DraftLook = Fresh;
		ShowLookPreview();
		break;
	}
	case EVC26MenuAction::LookDone:
		CloseLook();
		if (bRestyling)
		{
			Career->restyle(DraftLook);
		}
		else
		{
			// Keeps the previous career, in case this one was started by mistake.
			if (bHasSave)
			{
				IFileManager::Get().Copy(*(SavePath() + TEXT(".bak")), *SavePath());
			}
			Career.Emplace(TypedName.TrimStartAndEnd().IsEmpty() ? std::string("The Rookie") : std::string(TCHAR_TO_UTF8(*TypedName.TrimStartAndEnd())), GetChosenArchetype(), static_cast<std::uint32_t>(FDateTime::Now().GetTicks()), DraftLook);
		}
		SaveCareer();
		GoTo(EVC26Screen::Hub);
		break;
	case EVC26MenuAction::SignOffer:
		Career->startNextSeason(Item.Arg);
		SaveCareer();
		GoTo(EVC26Screen::Hub);
		break;
	case EVC26MenuAction::StoryChoice:
		Career->chooseStory(Item.Arg);
		SaveCareer();
		GoTo(Career->stage() == vc26::CareerStage::SeasonOver ? EVC26Screen::SeasonEnd : EVC26Screen::Hub);
		break;
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
		// Letters spell the name, so only the arrows change the position here.
		constexpr int32 MaxNameLength = 18;
		for (TCHAR Letter = TEXT('A'); Letter <= TEXT('Z'); ++Letter)
		{
			if (Player->WasInputKeyJustPressed(FKey(*FString::Chr(Letter))) && TypedName.Len() < MaxNameLength)
			{
				const bool bStartOfWord = TypedName.IsEmpty() || TypedName.EndsWith(TEXT(" "));
				TypedName.AppendChar(bStartOfWord ? Letter : FChar::ToLower(Letter));
			}
		}
		if (Pressed({EKeys::SpaceBar}) && !TypedName.IsEmpty() && !TypedName.EndsWith(TEXT(" ")) && TypedName.Len() < MaxNameLength)
		{
			TypedName.AppendChar(TEXT(' '));
		}
		if (Pressed({EKeys::BackSpace}))
		{
			TypedName.LeftChopInline(1);
		}
		if (Pressed({EKeys::Left, EKeys::Gamepad_DPad_Left}))
		{
			ChosenArchetype = (ChosenArchetype + vc26::kArchetypes - 1) % vc26::kArchetypes;
		}
		if (Pressed({EKeys::Right, EKeys::Gamepad_DPad_Right}))
		{
			ChosenArchetype = (ChosenArchetype + 1) % vc26::kArchetypes;
		}
	}
	if (Screen == EVC26Screen::Look)
	{
		const int32 Step = Pressed({EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left}) ? -1 : Pressed({EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right}) ? 1 : 0;
		const TArray<FVC26MenuItem> Items = GetMenuItems();
		if (Step != 0 && Items.IsValidIndex(MenuIndex) && Items[MenuIndex].Action == EVC26MenuAction::LookRow)
		{
			ChangeLookRow(static_cast<ELookRow>(Items[MenuIndex].Arg), Step);
		}
		ShowLookPreview();
	}
	if (Screen == EVC26Screen::Report && HasHighlights() && Pressed({EKeys::H, EKeys::Gamepad_FaceButton_Top}))
	{
		ReplayQueue = Highlights;
		ReplayReturn = EVC26Screen::Report;
		ParkedSim = MoveTemp(Sim);
		Screen = EVC26Screen::Match;
		PlayNextClip();
	}
	else if (Pressed({EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom}) || (Screen != EVC26Screen::CreatePlayer && Pressed({EKeys::SpaceBar})))
	{
		ConfirmMenu();
	}
	else if (Pressed({EKeys::Escape, EKeys::Gamepad_FaceButton_Right}))
	{
		if (Screen == EVC26Screen::Upgrades || Screen == EVC26Screen::Record)
		{
			GoTo(EVC26Screen::Hub);
		}
		else if (Screen == EVC26Screen::CreatePlayer)
		{
			GoTo(EVC26Screen::MainMenu);
		}
		else if (Screen == EVC26Screen::Look)
		{
			CloseLook();
			GoTo(bRestyling ? EVC26Screen::Hub : EVC26Screen::CreatePlayer);
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

	// In a couch match the arrows and gamepad belong to Player 2.
	const bool bShared = !bCouch;
	auto Held = [Player, bShared](const FKey& A, const FKey& B) { return Player->IsInputKeyDown(A) || (bShared && Player->IsInputKeyDown(B)); };

	Input.moveX = (Held(EKeys::W, EKeys::Up) ? 1.f : 0.f) - (Held(EKeys::S, EKeys::Down) ? 1.f : 0.f);
	Input.moveY = (Held(EKeys::A, EKeys::Left) ? 1.f : 0.f) - (Held(EKeys::D, EKeys::Right) ? 1.f : 0.f);
	if (bUseTouch)
	{
		const vc26::SimInput Fingers = Touch.input();
		Input.moveX += Fingers.moveX;
		Input.moveY += Fingers.moveY;
	}
	if (bShared)
	{
		Input.moveX += Player->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
		Input.moveY -= Player->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	}
	Input.moveX = FMath::Clamp(Input.moveX, -1.f, 1.f);
	Input.moveY = FMath::Clamp(Input.moveY, -1.f, 1.f);
	Input.action = Held(EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom) || (bUseTouch && Touch.actionHeld());
	return Input;
}

vc26::SimInput AVC26Arena::ReadAwayInput() const
{
	vc26::SimInput Input;
	const APlayerController* Player = Cast<APlayerController>(GetController());
	if (!Player || !bCouch)
	{
		return Input;
	}
	Input.moveX = (Player->IsInputKeyDown(EKeys::Up) ? 1.f : 0.f) - (Player->IsInputKeyDown(EKeys::Down) ? 1.f : 0.f) + Player->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	Input.moveY = (Player->IsInputKeyDown(EKeys::Left) ? 1.f : 0.f) - (Player->IsInputKeyDown(EKeys::Right) ? 1.f : 0.f) - Player->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	Input.moveX = FMath::Clamp(Input.moveX, -1.f, 1.f);
	Input.moveY = FMath::Clamp(Input.moveY, -1.f, 1.f);
	Input.action = Player->IsInputKeyDown(EKeys::Enter) || Player->IsInputKeyDown(EKeys::RightControl) || Player->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom);
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
	if (IsReplaying())
	{
		UpdateReplay(DeltaSeconds);
		HandleEvents();
		CommentaryTimeLeft = 0.f;  // the call belongs to the live point
		AnimationRate = ReplaySpeed;
		SyncShapes(DeltaSeconds);
		UpdateCamera(DeltaSeconds);
		Sounds.Tick(DeltaSeconds);
		if (!ShotDir.IsEmpty())
		{
			RunScreenshotTour(DeltaSeconds);
		}
		return;
	}

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
	const vc26::SimInput AwayInput = Screen == EVC26Screen::Match ? ReadAwayInput() : vc26::SimInput();
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
	Accumulator = GalleryPose >= 0 ? 0.f : FMath::Min(Accumulator + SimDelta, 0.25f);
	while (Accumulator >= SimStep)
	{
		Sim->step(SimStep, Input, AwayInput);
		InputLog.Add({Input, AwayInput});
		Accumulator -= SimStep;
	}

	SlowMoLeft = FMath::Max(SlowMoLeft - DeltaSeconds, 0.f);
	Sounds.SetMuted(Screen != EVC26Screen::Match);  // the match behind the menus stays silent
	HandleEvents();
	UpdateMoments(DeltaSeconds);
	if (IsReplaying())
	{
		return;  // a replay just took over
	}
	AnimationRate = SimDelta / FMath::Max(DeltaSeconds, UE_SMALL_NUMBER);
	SyncShapes(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
	Sounds.Tick(DeltaSeconds);
	BannerTimeLeft = FMath::Max(BannerTimeLeft - DeltaSeconds, 0.f);
	CommentaryTimeLeft = FMath::Max(CommentaryTimeLeft - DeltaSeconds, 0.f);

	if (!ShotDir.IsEmpty())
	{
		RunScreenshotTour(DeltaSeconds);
	}
}

void AVC26Arena::BeginClip(const FClip& Clip)
{
	Sim = MakeUnique<vc26::RallySim>(*Clip.Start);
	ApplyVenue(Sim->beach());  // the match behind a report may be indoors
	ReplayInputs = Clip.Inputs;
	ReplayCursor = 0;
	Accumulator = 0.f;
	bCameraReady = false;
}

void AVC26Arena::PlayNextClip()
{
	if (ReplayQueue.IsEmpty())
	{
		EndReplay();
		return;
	}
	const FClip Clip = ReplayQueue[0];
	ReplayQueue.RemoveAt(0);
	BeginClip(Clip);
}

void AVC26Arena::EndReplay()
{
	Sim = MoveTemp(ParkedSim);
	ApplyVenue(Sim->beach());
	ReplayQueue.Reset();
	Screen = ReplayReturn;
	Accumulator = 0.f;
	bCameraReady = false;
}

void AVC26Arena::UpdateReplay(float DeltaSeconds)
{
	const APlayerController* Player = Cast<APlayerController>(GetController());
	if (Player && (Player->WasInputKeyJustPressed(EKeys::SpaceBar) || Player->WasInputKeyJustPressed(EKeys::Enter) ||
		Player->WasInputKeyJustPressed(EKeys::Escape) || Player->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)))
	{
		EndReplay();
		return;
	}

	Accumulator = FMath::Min(Accumulator + DeltaSeconds * ReplaySpeed, 0.25f);
	while (Accumulator >= SimStep && ReplayCursor < ReplayInputs.Num())
	{
		const FLoggedInput& Logged = ReplayInputs[ReplayCursor++];
		Sim->step(SimStep, Logged.Home, Logged.Away);
		Accumulator -= SimStep;
	}
	if (ReplayCursor >= ReplayInputs.Num())
	{
		PlayNextClip();
	}
}

void AVC26Arena::UpdateMoments(float DeltaSeconds)
{
	if (Screen != EVC26Screen::Match || !Sim->humanHome())
	{
		return;
	}

	// The crowd stays up while a team is in the Zone.
	if (Sim->inZone(vc26::Team::Home) || Sim->inZone(vc26::Team::Away))
	{
		CrowdExcitement = FMath::Max(CrowdExcitement, 0.22f);
	}

	// Set point: a hush, a heartbeat and a tighter shot.
	const bool bPressure = Sim->pressurePoint() && Sim->phase() == vc26::RallyPhase::PreServe;
	if (bPressure && !bPressureAnnounced)
	{
		SetBanner(TEXT("SET POINT"));
		HeartbeatTimer = 0.f;
	}
	bPressureAnnounced = bPressure;
	if (bPressure)
	{
		HeartbeatTimer -= DeltaSeconds;
		if (HeartbeatTimer <= 0.f)
		{
			Sounds.Play(this, EVC26Sound::Heartbeat, 0.8f);
			HeartbeatTimer = 0.8f;
		}
	}
	Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, bPressure ? 50.f : 62.f, DeltaSeconds, 2.5f));

	if (ReplayCountdown >= 0.f)
	{
		ReplayCountdown -= DeltaSeconds;
		if (ReplayCountdown < 0.f && Snapshot.IsValid())
		{
			const FClip Clip{Snapshot, InputLog, RallyExcitement};
			Highlights.Add(Clip);
			Highlights.Sort([](const FClip& A, const FClip& B) { return A.Excitement > B.Excitement; });
			Highlights.SetNum(FMath::Min(Highlights.Num(), 3));

			// Only the best rallies stop the match for a replay.
			if (RallyExcitement >= (ShotDir.IsEmpty() ? 5 : 1))
			{
				ReplayQueue = {Clip};
				ReplayReturn = EVC26Screen::Match;
				ParkedSim = MoveTemp(Sim);
				PlayNextClip();
			}
		}
	}
}

void AVC26Arena::SetBanner(const FString& Text)
{
	if (IsReplaying())
	{
		return;
	}
	Banner = Text;
	BannerTimeLeft = 1.6f;
}

void AVC26Arena::HandleEvents()
{
	const bool bLive = !IsReplaying();
	bool bTakeSnapshot = false;
	for (const vc26::SimEvent& Event : Sim->takeEvents())
	{
		if (bLive)
		{
			vc26::recordEvent(Stats, Event, LockedPlayer);
		}

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
			bTakeSnapshot = bTakeSnapshot || Event.kind == vc26::ContactKind::Serve || Event.kind == vc26::ContactKind::Set;
			if (bLive)
			{
				LastTouch[static_cast<int32>(Event.team)] = Event.player;
			}
			FFigure& Figure = Figures[Event.player];
			Figure.Swing = Swing;
			Figure.SwingLeft = 0.28f;
			Figure.Reach = SimToWorld(Event.where);
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
			else if (Event.team == vc26::Team::Home && Event.kind != vc26::ContactKind::Serve && Sim->humanHome() && (LockedPlayer < 0 || Event.player == LockedPlayer))
			{
				SetBanner(QualityText(Event.quality));
			}
			break;
		}
		case vc26::SimEvent::Type::Whiff:
			if (Event.team == vc26::Team::Home && Sim->humanHome() && (LockedPlayer < 0 || Event.player == LockedPlayer))
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
			if (bLive && Sim->humanHome())
			{
				RallyExcitement = Sim->contactsThisRally() / 3 + (Event.clutch ? 3 : 0);
				RallyExcitement += Event.reason == vc26::PointReason::Ace ? 3 : Event.reason == vc26::PointReason::Block ? 4 : Event.reason == vc26::PointReason::Kill ? 1 : 0;
				ReplayCountdown = 1.1f;  // lets the ball land and bounce in the clip
			}
			if (Sim->ball().pos.z < 0.5f)
			{
				Sounds.Play(this, EVC26Sound::Floor);
			}
			Sounds.Play(this, EVC26Sound::Whistle, 0.35f);
			// Long rallies get the bigger reaction (GDD 4).
			Sounds.Play(this, EVC26Sound::Cheer, 0.25f + 0.5f * FMath::Min(Sim->contactsThisRally() / 12.f, 1.f));
			SetBanner(FString::Printf(TEXT("%s  %s POINT"), ReasonText(Event.reason), Event.team == vc26::Team::Home ? *HomeName.ToUpper() : *AwayName.ToUpper()));
			if (bLive)
			{
				CallPoint(Event);
			}

			const vc26::MatchScore& Score = Sim->score();
			UE_LOG(LogVC26, Log, TEXT("Point: %s %s | contacts %d | set %d | %d-%d"), Event.team == vc26::Team::Home ? TEXT("Home") : TEXT("Away"),
				ReasonText(Event.reason), Sim->contactsThisRally(), Score.setNumber(), Score.points(vc26::Team::Home), Score.points(vc26::Team::Away));
			break;
		}
		case vc26::SimEvent::Type::ZoneStart:
			Sounds.Play(this, EVC26Sound::Cheer, 0.6f);
			SetBanner(Event.team == vc26::Team::Home ? TEXT("IN THE ZONE!") : FString::Printf(TEXT("%s IN THE ZONE"), *AwayName.ToUpper()));
			break;
		}
	}

	if (bTakeSnapshot && bLive && Sim->humanHome())
	{
		Snapshot = MakeShared<vc26::RallySim>(*Sim);
		InputLog.Reset();
	}
}

void AVC26Arena::RunScreenshotTour(float DeltaSeconds)
{
	TourTime += DeltaSeconds;
	++TourFrames;
	TourSlowest = FMath::Max(TourSlowest, DeltaSeconds);
	TourSeconds += DeltaSeconds;
	auto Shot = [this](const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(ShotDir / FString(Name) + TEXT(".png"), true, false);
		UE_LOG(LogVC26, Log, TEXT("Tour shot: %s (%.1f ms a frame, slowest %.1f)"), Name, TourSeconds * 1000.f / FMath::Max(TourFrames, 1), TourSlowest * 1000.f);
		TourFrames = 0;
		TourSeconds = 0.f;
		TourSlowest = 0.f;
	};

	if (!bCommentaryShot && CommentaryTimeLeft > 3.f && Screen == EVC26Screen::Match && !IsReplaying() && Sim->humanHome())
	{
		bCommentaryShot = true;
		Shot(TEXT("14_commentary"));
	}

	// The last steps wait for a moment in a live match, with a time limit.
	if (TourStep >= 16 && TourStep <= 18)
	{
		const vc26::PendingContact& Pending = Sim->pending();
		const bool bRing = TourStep == 16 && Pending.active && Pending.team == vc26::Team::Home && Pending.timeToContact < 0.5f;
		const bool bBlock = TourStep == 17 && Sim->humanBlocking() && Sim->block().timeToContact < 0.6f;
		const bool bReplay = TourStep == 18 && IsReplaying() && ReplayCursor > 120;
		if (bRing || bBlock || bReplay || TourTime > 40.f)
		{
			Shot(TourStep == 16 ? TEXT("10_rings") : TourStep == 17 ? TEXT("11_block") : TEXT("12_replay"));
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
	case 19:
		if (IsReplaying())
		{
			EndReplay();
		}
		StartMatch(false, EVC26Mode::Beach);
		break;
	case 20: Shot(TEXT("13_beach")); StartBackgroundMatch(); GoTo(EVC26Screen::Match); break;
	case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
	{
		// Everyone holds a pose for a close look, with the sim frozen.
		static const TCHAR* const Names[] = {TEXT("20_ready"), TEXT("21_pass"), TEXT("22_set"), TEXT("23_block"), TEXT("24_windup"), TEXT("24b_draw"), TEXT("25_swing"), TEXT("26_hold"), TEXT("26b_toss"), TEXT("27_cheer")};
		static const EPose Poses[] = {EPose::Idle, EPose::Pass, EPose::Set, EPose::Block, EPose::AttackWindUp, EPose::AttackDraw, EPose::AttackSwing, EPose::ServeHold, EPose::ServeToss, EPose::Celebrate};
		if (Screen != EVC26Screen::Match)
		{
			StartBackgroundMatch();
			GoTo(EVC26Screen::Match);
		}
		const int32 Index = TourStep - 23;
		if (Index >= 0)
		{
			Shot(Names[Index]);
		}
		if (Index + 1 < UE_ARRAY_COUNT(Poses))
		{
			const EPose Next = Poses[Index + 1];
			GalleryPose = static_cast<int32>(Next);
			GalleryAim = Next == EPose::AttackDraw || Next == EPose::AttackSwing || Next == EPose::Block ? 200.f : 105.f;
		}
		break;
	}
	case 32:
		GalleryPose = -1;
		Career.Emplace(std::string("The Rookie"), vc26::Archetype::PowerSpiker, 7u);
		while (Career->stage() != vc26::CareerStage::SeasonOver)
		{
			vc26::MatchStats Great = DemoStats();
			Great.perfect = 40;
			Great.kills = 16;
			Career->recordMatch(2, 0, Great);
		}
		GoTo(EVC26Screen::Transfers);
		break;
	case 33: Shot(TEXT("30_transfers")); break;
	case 34: GoTo(EVC26Screen::Record); break;
	case 35: Shot(TEXT("31_record")); break;
	case 36: Report = Career->sitOut(); GoTo(EVC26Screen::Report); break;
	case 37: Shot(TEXT("32_sat_out")); StartBackgroundMatch(); break;
	case 38: OpenLook(false); break;
	case 39: Shot(TEXT("40_look_body")); MenuIndex = static_cast<int32>(ELookRow::Nose); break;
	case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
	{
		// Faces from a squad, then the same again with a lean and a heavy build.
		const int32 Index = TourStep - 41;
		if (Index >= 0)
		{
			Shot(*FString::Printf(TEXT("41_face_%d"), Index));
		}
		DraftLook = vc26::makeTeamAppearance("HOME", Index + 2);
		DraftLook.hair = static_cast<vc26::HairStyle>((Index + 1) % vc26::kHairStyles);
		DraftLook.facialHair = static_cast<vc26::FacialHair>((Index + 1) % vc26::kFacialHairs);
		break;
	}
	case 48:
		MenuIndex = 0;
		DraftLook.heightCm = 214;
		DraftLook.massKg = 88;
		break;
	case 49: Shot(TEXT("42_body_tall_lean")); break;
	case 50: DraftLook.heightCm = 176; DraftLook.massKg = 92; break;
	case 51: Shot(TEXT("43_body_short_heavy")); break;
	case 52: CloseLook(); GoTo(EVC26Screen::MainMenu); break;
	default: FPlatformMisc::RequestExit(false, TEXT("VC26 screenshot tour")); break;
	}
}
