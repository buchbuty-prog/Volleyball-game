// The game's stage: runs the menus and career, drives a vc26::RallySim and draws
// it. All the rules live in core/. The hall, crowd and players are built from
// engine shapes at runtime, so the prototype needs no content assets.
// VC26Arena.cpp holds the flow, VC26ArenaVisuals.cpp the drawing.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Templates/UniquePtr.h"
#include "VC26AthleteAnim.h"
#include "VC26Sounds.h"
#include "vc26/Career.h"
#include "vc26/RallySim.h"
#include "vc26/TouchControls.h"
#include "VC26Arena.generated.h"

class UCameraComponent;
class UAnimSequence;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;

// core/ is right-handed and Unreal is left-handed, so y flips. This keeps Home's
// left on the left of the screen. Metres become centimetres.
inline FVector SimToWorld(const vc26::Vec3& V)
{
	return FVector(V.x, -V.y, V.z) * 100.f;
}

enum class EVC26Screen : uint8
{
	MainMenu,
	CreatePlayer,
	Hub,
	Upgrades,
	Match,
	Report,
	SeasonEnd,
	Story
};

enum class EVC26Mode : uint8
{
	Indoor,
	Beach,
	Couch
};

enum class EVC26MenuAction : uint8
{
	ContinueCareer,
	NewCareer,
	QuickMatch,
	BeachMatch,
	CouchMatch,
	Quit,
	PlayNext,
	OpenUpgrades,
	ToMainMenu,
	Upgrade,  // Arg is the skill
	Back,
	StoryChoice  // Arg is the option
};

struct FVC26MenuItem
{
	FString Label;
	EVC26MenuAction Action;
	int32 Arg = 0;
};

UCLASS()
class AVC26Arena : public APawn
{
	GENERATED_BODY()

public:
	AVC26Arena();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	const vc26::RallySim* GetSim() const { return Sim.Get(); }
	const FString& GetBanner() const { return Banner; }
	float GetBannerTimeLeft() const { return BannerTimeLeft; }
	const TCHAR* GetDifficultyName() const;
	const vc26::TouchControls* GetTouch() const { return bUseTouch ? &Touch : nullptr; }

	bool IsReplaying() const { return ParkedSim.IsValid(); }
	bool HasHighlights() const { return !Highlights.IsEmpty(); }

	EVC26Screen GetScreen() const { return Screen; }
	int32 GetMenuIndex() const { return MenuIndex; }
	TArray<FVC26MenuItem> GetMenuItems() const;
	const vc26::Career* GetCareer() const { return Career.IsSet() ? &Career.GetValue() : nullptr; }
	const vc26::MatchReport& GetReport() const { return Report; }
	vc26::Archetype GetChosenArchetype() const { return static_cast<vc26::Archetype>(ChosenArchetype); }
	bool IsCareerMatch() const { return bCareerMatch; }
	bool IsCouchMatch() const { return bCouch; }
	const FString& GetHomeName() const { return HomeName; }
	const FString& GetAwayName() const { return AwayName; }

private:
	enum class EPose : uint8 { Idle, Pass, Set, AttackWindUp, AttackSwing, Block, ServeToss, Dive, Celebrate };

	// What a rigged athlete is playing underneath. Spike starts early for a wind-up,
	// Swing late for a hit with none. Passes, sets and blocks are built on Idle.
	enum class EClip : uint8 { Idle, Run, Spike, Swing, Dive, Samba, Count };

	// Per-player animation state. The shapes themselves live in Parts.
	struct FFigure
	{
		FVector LastFeet = FVector::ZeroVector;
		FVector Facing = FVector::ForwardVector;
		float StridePhase = 0.f;
		float RunAmount = 0.f;
		float Height = 1.f;
		EPose Swing = EPose::Idle;  // held briefly after touching the ball
		float SwingLeft = 0.f;
		FVector DiveDirection = FVector::ForwardVector;
		int32 ShownId = -1;
		EClip Clip = EClip::Count;
		float FadeLeft = 0.f;
		float FadeTime = 0.f;
		FVector Body = FVector::ForwardVector;  // the way the hips point
		FVector Reach = FVector::ZeroVector;    // where the ball was last met
		FVC26AthleteDrive Drive;
		bool bShown = true;
	};

	// Flow (VC26Arena.cpp).
	void StartBackgroundMatch();
	void StartMatch(bool bCareer, EVC26Mode Mode = EVC26Mode::Indoor);
	void FinishMatch();
	void LeaveMatch();
	void GoTo(EVC26Screen NewScreen);
	void UpdateMenus();
	void ConfirmMenu();
	void SaveCareer();
	bool LoadCareer();
	void UpdateTouch();
	vc26::SimInput ReadInput() const;
	vc26::SimInput ReadAwayInput() const;
	void HandleEvents();
	void SetBanner(const FString& Text);
	void SetDifficulty(int32 Level);
	void RunScreenshotTour(float DeltaSeconds);

	// Replays (the sim is deterministic, so a copy plus the input log rebuilds a rally).
	struct FLoggedInput
	{
		vc26::SimInput Home;
		vc26::SimInput Away;
	};
	struct FClip
	{
		TSharedPtr<vc26::RallySim> Start;
		TArray<FLoggedInput> Inputs;
		int32 Excitement = 0;
	};
	void BeginClip(const FClip& Clip);
	void PlayNextClip();
	void EndReplay();
	void UpdateReplay(float DeltaSeconds);
	void UpdateMoments(float DeltaSeconds);

	// Drawing (VC26ArenaVisuals.cpp).
	UMaterialInstanceDynamic* ColorMaterial(const FLinearColor& Color);
	// bStatic shapes feed Lumen's distance fields; moving or very thin ones shouldn't.
	UStaticMeshComponent* AddShape(UStaticMesh* Mesh, const FVector& CentreCm, const FVector& SizeCm, const FLinearColor& Color, bool bCastShadow = true, bool bStatic = true);
	void AddLine(float X, float Y, float LengthX, float LengthY);
	void AddBoard(const FVector& Centre, const FVector& Size, const FRotator& TextFacing);
	void AddCrowdBlock(const FVector& Start, const FVector& AlongRow, const FVector& OutwardStep, int32 Seats);
	void BuildHall();
	void BuildCourt();
	void BuildCrowd();
	void BuildPlayers();
	bool BuildAthletes();
	void AnimateAthlete(int32 Player, EPose Pose, const FVector& Feet, const FVector& Forward, const FVector& MoveDir, float Speed, float DeltaSeconds);
	void BuildBall();
	void BuildLights();
	void ApplyVenue(bool bBeach);
	void SyncShapes(float DeltaSeconds);
	EPose PoseFor(int32 Player) const;
	void AnimateFigure(int32 Player, float DeltaSeconds);
	void PlaceBone(int32 Part, const FVector& From, const FVector& To, float Thickness);
	void UpdateBall(float DeltaSeconds);
	void UpdateCrowd(float DeltaSeconds);
	void UpdateRings();
	void PlaceRing(const TArray<TObjectPtr<UStaticMeshComponent>>& Ring, const FVector& Centre, float Radius, bool bVisible);
	void BuildRing(TArray<TObjectPtr<UStaticMeshComponent>>& Ring, const FLinearColor& Color);
	void UpdateCamera(float DeltaSeconds);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	// One material per colour, so identical shapes can share draw calls.
	UPROPERTY()
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> MaterialByColor;

	// EPart::Count shapes per player.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UPROPERTY()
	TArray<TObjectPtr<UTextRenderComponent>> Numbers;

	// Rigged players, when the Mixamo assets are present. Empty otherwise.
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> Athletes;

	UPROPERTY()
	TArray<TObjectPtr<UAnimSequence>> ClipAnims;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BallShape;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Trail;

	// Floor rings: where to be, the same ring once in position, and the one that
	// closes in. Pressing as it meets the others is perfect timing.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TargetRing;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ReadyRing;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TimingRing;

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> CrowdGroups;

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> CrowdArms;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ControlledMarker;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> AwayMarker;

	// Swapped between the indoor hall and the beach.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> RoofShapes;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> WallShapes;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Sand;

	TUniquePtr<vc26::RallySim> Sim;
	TUniquePtr<vc26::RallySim> ParkedSim;  // the live match, while a replay plays in Sim
	TSharedPtr<vc26::RallySim> Snapshot;   // the rally at its last set or serve
	TArray<FLoggedInput> InputLog;         // everything since Snapshot
	TArray<FClip> ReplayQueue;
	TArray<FClip> Highlights;
	TArray<FLoggedInput> ReplayInputs;
	int32 ReplayCursor = 0;
	EVC26Screen ReplayReturn = EVC26Screen::Match;
	int32 RallyExcitement = 0;
	int32 LockedPlayer = -1;
	float ReplayCountdown = -1.f;
	float HeartbeatTimer = 0.f;
	bool bPressureAnnounced = false;
	TOptional<vc26::Career> Career;
	vc26::MatchStats Stats;
	vc26::MatchReport Report;
	EVC26Screen Screen = EVC26Screen::MainMenu;
	int32 MenuIndex = 0;
	int32 ChosenArchetype = 0;
	bool bCareerMatch = false;
	bool bCouch = false;
	FString HomeName = TEXT("HOME");
	FString AwayName = TEXT("AWAY");

	float Accumulator = 0.f;
	FString Banner;
	float BannerTimeLeft = 0.f;
	bool bCameraReady = false;
	FVector CameraPosition = FVector::ZeroVector;
	float OrbitAngle = 0.f;
	float ShakeLeft = 0.f;
	float SlowMoLeft = 0.f;
	float AnimationRate = 1.f;  // how fast the players move next to real time

	// The screenshot tour can freeze the match and pose everyone.
	int32 GalleryPose = -1;
	int32 GalleryPlayer = 0;
	FVector GalleryView = FVector(230.f, -230.f, 140.f);
	float BallSpin = 0.f;
	float CrowdExcitement = 0.f;
	bool bCrowdSettled = true;
	bool bCrowdArmsUp = false;
	float CameraSide = 0.f;
	TArray<FVector> TrailPoints;
	int32 Difficulty = 1;  // 0 easy, 1 normal, 2 hard
	bool bUseTouch = false;
	bool bHasSave = false;
	bool bTouchWasHeld = false;
	FString ShotDir;
	float TourTime = 0.f;
	int32 TourFrames = 0;
	float TourSeconds = 0.f;
	float TourSlowest = 0.f;
	int32 TourStep = 0;
	vc26::TouchControls Touch;
	FFigure Figures[12];
	FVC26Sounds Sounds;
};
