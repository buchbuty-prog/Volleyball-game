#include "VC26Arena.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogVC26, Log, All);

namespace
{
	constexpr float SimStep = 1.f / 120.f;
	constexpr float MetresToCm = 100.f;
	constexpr float CpuSkillLevels[] = {0.35f, 0.7f, 1.f};
	constexpr float SlowMoScale = 0.4f;  // GDD 1.1: 0.4x for 300 ms on a power spike
	constexpr float SlowMoSeconds = 0.3f;

	const FLinearColor HomeColor(0.05f, 0.25f, 0.85f);
	const FLinearColor AwayColor(0.8f, 0.08f, 0.08f);
	const FLinearColor HeadColor(0.85f, 0.6f, 0.45f);

	// core/ is right-handed and Unreal is left-handed, so y flips. This keeps
	// Home's left on the left of the screen.
	FVector ToWorld(const vc26::Vec3& V)
	{
		return FVector(V.x, -V.y, V.z) * MetresToCm;
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
	Camera->SetFieldOfView(65.f);  // GDD 1.1
}

UStaticMeshComponent* AVC26Arena::AddShape(UStaticMesh* Mesh, const FVector& CentreCm, const FVector& SizeCm, const FLinearColor& Color, bool bCastShadow)
{
	static UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(this);
	Shape->SetMobility(EComponentMobility::Movable);
	Shape->SetStaticMesh(Mesh);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCastShadow(bCastShadow);
	Shape->SetupAttachment(Root);
	Shape->RegisterComponent();
	Shape->SetWorldLocation(CentreCm);
	Shape->SetWorldScale3D(SizeCm / 100.f);  // the engine's basic shapes are 1 m across

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	Shape->SetMaterial(0, Material);
	return Shape;
}

void AVC26Arena::AddLine(float X, float Y, float LengthX, float LengthY)
{
	AddShape(CubeMesh, FVector(X, -Y, 0.f) * MetresToCm + FVector(0, 0, 2.f), FVector(LengthX, LengthY, 0.01f) * MetresToCm, FLinearColor::White, false);
}

void AVC26Arena::BuildCourt()
{
	const float Length = vc26::kCourtHalfLength;
	const float Width = vc26::kCourtHalfWidth;
	const float LineWidth = 0.05f;

	// Hall.
	AddShape(CubeMesh, FVector(0, 0, -10.f), FVector(3400.f, 2200.f, 20.f), FLinearColor(0.03f, 0.22f, 0.3f), false);
	const FLinearColor WallColor(0.1f, 0.12f, 0.18f);
	AddShape(CubeMesh, FVector(1700.f, 0, 450.f), FVector(20.f, 2200.f, 900.f), WallColor, false);
	AddShape(CubeMesh, FVector(-1700.f, 0, 450.f), FVector(20.f, 2200.f, 900.f), WallColor, false);
	AddShape(CubeMesh, FVector(0, 1100.f, 450.f), FVector(3400.f, 20.f, 900.f), WallColor, false);
	AddShape(CubeMesh, FVector(0, -1100.f, 450.f), FVector(3400.f, 20.f, 900.f), WallColor, false);

	// Playing surface and lines.
	AddShape(CubeMesh, FVector(0, 0, 0.5f), FVector(Length * 2.f, Width * 2.f, 0.01f) * MetresToCm, FLinearColor(0.75f, 0.35f, 0.1f), false);
	AddLine(-Length, 0.f, LineWidth, Width * 2.f);
	AddLine(Length, 0.f, LineWidth, Width * 2.f);
	AddLine(0.f, Width, Length * 2.f, LineWidth);
	AddLine(0.f, -Width, Length * 2.f, LineWidth);
	AddLine(0.f, 0.f, LineWidth, Width * 2.f);
	AddLine(-3.f, 0.f, LineWidth, Width * 2.f);
	AddLine(3.f, 0.f, LineWidth, Width * 2.f);

	// Net: a 1 m open mesh of thin strips, so the far team stays visible. The
	// sim treats everything below the tape as net too.
	const float NetTop = vc26::kNetHeight * MetresToCm;
	const float NetWidth = Width * 2.f * MetresToCm;
	const float PostY = (Width + 0.5f) * MetresToCm;
	const FLinearColor MeshColor(0.02f, 0.02f, 0.02f);
	for (float Y = -NetWidth / 2.f; Y <= NetWidth / 2.f; Y += 30.f)
	{
		AddShape(CubeMesh, FVector(0, Y, NetTop - 50.f), FVector(1.f, 1.5f, 100.f), MeshColor, false);
	}
	for (float Z = NetTop - 100.f; Z < NetTop; Z += 25.f)
	{
		AddShape(CubeMesh, FVector(0, 0, Z), FVector(1.f, NetWidth, 1.5f), MeshColor, false);
	}
	AddShape(CubeMesh, FVector(0, 0, NetTop - 100.f), FVector(2.f, NetWidth, 5.f), FLinearColor::White, false);
	AddShape(CubeMesh, FVector(0, 0, NetTop - 3.f), FVector(3.f, NetWidth, 6.f), FLinearColor::White, false);
	for (const float Y : {-PostY, PostY})
	{
		AddShape(CylinderMesh, FVector(0, Y, NetTop / 2.f), FVector(10.f, 10.f, NetTop), FLinearColor(0.6f, 0.6f, 0.65f));
	}
	for (const float Y : {-Width * MetresToCm, Width * MetresToCm})
	{
		AddShape(CylinderMesh, FVector(0, Y, NetTop + 20.f), FVector(2.f, 2.f, 180.f), FLinearColor(0.9f, 0.1f, 0.1f), false);
	}

	// Moving pieces.
	for (int32 i = 0; i < 12; ++i)
	{
		Limbs.Add(AddShape(CylinderMesh, FVector::ZeroVector, FVector(100.f), i < 6 ? HomeColor : AwayColor));
		Limbs.Add(AddShape(SphereMesh, FVector::ZeroVector, FVector(26.f), HeadColor));
		for (int32 Limb = 0; Limb < 4; ++Limb)
		{
			Limbs.Add(AddShape(CylinderMesh, FVector::ZeroVector, FVector(100.f), HeadColor));
		}
	}
	// Drawn a little larger than a real ball so it reads at a distance.
	BallShape = AddShape(SphereMesh, FVector::ZeroVector, FVector(32.f), FLinearColor(1.f, 0.85f, 0.1f));
	LandingMarker = AddShape(CylinderMesh, FVector::ZeroVector, FVector(240.f, 240.f, 1.f), FLinearColor(0.9f, 0.9f, 0.9f), false);
	ControlledMarker = AddShape(CylinderMesh, FVector::ZeroVector, FVector(100.f, 100.f, 1.f), FLinearColor(1.f, 0.9f, 0.f), false);
}

void AVC26Arena::BuildLights()
{
	auto AddLight = [this](const FRotator& Direction, float Intensity, bool bShadows)
	{
		UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(this);
		Light->SetMobility(EComponentMobility::Movable);
		Light->ForwardShadingPriority = bShadows ? 1 : 0;
		Light->SetupAttachment(Root);
		Light->RegisterComponent();
		Light->SetWorldRotation(Direction);
		Light->SetIntensity(Intensity);
		Light->SetCastShadows(bShadows);
	};
	AddLight(FRotator(-70.f, 30.f, 0.f), 1.6f, true);
	AddLight(FRotator(-35.f, 200.f, 0.f), 0.6f, false);
}

void AVC26Arena::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	BuildCourt();
	BuildLights();
	Sounds.Init();
	NewMatch();
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

void AVC26Arena::NewMatch()
{
	vc26::RallySim::Config Config;
	Config.seed = static_cast<std::uint32_t>(FDateTime::Now().GetTicks());
	Config.humanHome = !FParse::Param(FCommandLine::Get(), TEXT("VC26Autoplay"));
	Config.cpuSkill = CpuSkillLevels[Difficulty];
	Sim = MakeUnique<vc26::RallySim>(Config);
	Accumulator = 0.f;
	bCameraReady = false;
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
	Input.moveX += Player->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	Input.moveY -= Player->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	Input.moveX = FMath::Clamp(Input.moveX, -1.f, 1.f);
	Input.moveY = FMath::Clamp(Input.moveY, -1.f, 1.f);
	Input.action = Held(EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom);
	return Input;
}

void AVC26Arena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Sim)
	{
		return;
	}

	const vc26::SimInput Input = ReadInput();
	if (Sim->phase() == vc26::RallyPhase::MatchOver && Input.action)
	{
		NewMatch();
	}

	if (const APlayerController* Player = Cast<APlayerController>(GetController()))
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

	// Fixed steps keep the timing windows and results independent of frame rate.
	const float SimDelta = SlowMoLeft > 0.f ? DeltaSeconds * SlowMoScale : DeltaSeconds;
	Accumulator = FMath::Min(Accumulator + SimDelta, 0.25f);
	while (Accumulator >= SimStep)
	{
		Sim->step(SimStep, Input);
		Accumulator -= SimStep;
	}

	SlowMoLeft = FMath::Max(SlowMoLeft - DeltaSeconds, 0.f);
	HandleEvents();
	SyncShapes(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
	Sounds.Tick(DeltaSeconds);
	BannerTimeLeft = FMath::Max(BannerTimeLeft - DeltaSeconds, 0.f);
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
		switch (Event.type)
		{
		case vc26::SimEvent::Type::Contact:
			switch (Event.kind)
			{
			case vc26::ContactKind::Serve:
				Sounds.Play(this, EVC26Sound::Spike, 0.6f, 1.2f);
				Figures[Event.player].Swing = EPose::AttackSwing;
				break;
			case vc26::ContactKind::Pass:
				Sounds.Play(this, EVC26Sound::Pass);
				Figures[Event.player].Swing = EPose::Pass;
				break;
			case vc26::ContactKind::Set:
			case vc26::ContactKind::FreeBall:
				Sounds.Play(this, EVC26Sound::Set);
				Figures[Event.player].Swing = EPose::Set;
				break;
			case vc26::ContactKind::Attack:
				Sounds.Play(this, EVC26Sound::Spike, Event.quality == vc26::ContactQuality::Shank ? 0.5f : 1.f);
				Figures[Event.player].Swing = EPose::AttackSwing;
				ShakeLeft = Event.quality == vc26::ContactQuality::Perfect ? 0.25f : 0.12f;
				if (Event.quality == vc26::ContactQuality::Perfect && Event.team == vc26::Team::Home && Sim->humanHome())
				{
					SlowMoLeft = SlowMoSeconds;
				}
				break;
			case vc26::ContactKind::Block:
				Sounds.Play(this, EVC26Sound::Block);
				Figures[Event.player].Swing = EPose::Block;
				break;
			}
			Figures[Event.player].SwingLeft = 0.28f;

			if (Event.kind == vc26::ContactKind::Block)
			{
				SetBanner(Event.quality == vc26::ContactQuality::Perfect ? TEXT("ROOF BLOCK!") : TEXT("BLOCK TOUCH"));
			}
			else if (Event.team == vc26::Team::Home && Event.kind != vc26::ContactKind::Serve && Sim->humanHome())
			{
				SetBanner(QualityText(Event.quality));
			}
			break;
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
			if (Sim->ball().pos.z < 0.5f)
			{
				Sounds.Play(this, EVC26Sound::Floor);
			}
			Sounds.Play(this, EVC26Sound::Whistle, 0.35f);
			// Long rallies get the bigger reaction (GDD 4).
			Sounds.Play(this, EVC26Sound::Cheer, 0.25f + 0.5f * FMath::Min(Sim->contactsThisRally() / 12.f, 1.f));

			const vc26::MatchScore& Score = Sim->score();
			SetBanner(FString::Printf(TEXT("%s  %s POINT"), ReasonText(Event.reason), Event.team == vc26::Team::Home ? TEXT("HOME") : TEXT("AWAY")));
			UE_LOG(LogVC26, Log, TEXT("Point: %s %s | contacts %d | set %d | %d-%d"), Event.team == vc26::Team::Home ? TEXT("Home") : TEXT("Away"),
				ReasonText(Event.reason), Sim->contactsThisRally(), Score.setNumber(), Score.points(vc26::Team::Home), Score.points(vc26::Team::Away));
			break;
		}
		}
	}
}

AVC26Arena::EPose AVC26Arena::PoseFor(int32 Player) const
{
	const vc26::BlockAttempt& Block = Sim->block();
	if (Block.active && Block.player == Player && (Block.committed || Block.timeToContact < 0.6f))
	{
		return EPose::Block;
	}

	const vc26::PendingContact& Pending = Sim->pending();
	if (Pending.active && Pending.player == Player && Pending.timeToContact < 0.55f)
	{
		if (Pending.touch == 1)
		{
			return EPose::Pass;
		}
		return Pending.touch == 3 && Pending.point.z >= 2.5f ? EPose::AttackWindUp : EPose::Set;
	}

	if (Sim->serveCharging() && Sim->server() == Player)
	{
		return EPose::ServeToss;
	}
	return Figures[Player].SwingLeft > 0.f ? Figures[Player].Swing : EPose::Idle;
}

void AVC26Arena::PlaceLimb(int32 Limb, const FVector& From, const FVector& To, float Thickness)
{
	const FVector Along = To - From;
	Limbs[Limb]->SetWorldLocationAndRotation((From + To) / 2.f, FRotationMatrix::MakeFromZ(Along).Rotator());
	Limbs[Limb]->SetWorldScale3D(FVector(Thickness / 100.f, Thickness / 100.f, Along.Size() / 100.f));
}

void AVC26Arena::AnimateFigure(int32 Player, float DeltaSeconds)
{
	FFigure& Figure = Figures[Player];
	const FVector Feet = ToWorld(Sim->players()[Player].pos);
	const FVector Up = FVector::UpVector;

	// Stride comes from how far the feet actually moved.
	FVector Moved = Feet - Figure.LastFeet;
	Moved.Z = 0.f;
	Figure.LastFeet = Feet;
	const float Speed = DeltaSeconds > 0.f ? Moved.Size() / DeltaSeconds : 0.f;
	const FVector MoveDir = Moved.GetSafeNormal();
	Figure.StridePhase += Moved.Size() * 0.035f;
	Figure.RunAmount = FMath::FInterpTo(Figure.RunAmount, FMath::Clamp(Speed / 500.f, 0.f, 1.f), DeltaSeconds, 12.f);
	Figure.SwingLeft = FMath::Max(Figure.SwingLeft - DeltaSeconds, 0.f);

	// Turn to the ball, or the net when it's right overhead.
	FVector ToBall = ToWorld(Sim->ball().pos) - Feet;
	ToBall.Z = 0.f;
	const FVector NetDirection(Player < 6 ? 1.f : -1.f, 0.f, 0.f);
	const FVector WantedFacing = ToBall.Size() > 60.f ? ToBall.GetSafeNormal() : NetDirection;
	Figure.Facing = (Figure.Facing + (WantedFacing - Figure.Facing) * FMath::Min(DeltaSeconds * 10.f, 1.f)).GetSafeNormal(UE_SMALL_NUMBER, NetDirection);
	const FVector Forward = Figure.Facing;
	const FVector Side = FVector::CrossProduct(Up, Forward);

	const EPose Pose = PoseFor(Player);
	const float Crouch = Pose == EPose::Pass ? 28.f : 0.f;
	const FVector Hip = Feet + Up * (92.f - Crouch);
	const FVector Neck = Feet + Up * (150.f - Crouch * 1.3f) + Forward * Crouch * 0.5f;
	const FVector ShoulderL = Neck - Side * 22.f;
	const FVector ShoulderR = Neck + Side * 22.f;

	const float Swing = FMath::Sin(Figure.StridePhase) * Figure.RunAmount;
	const float Lift = FMath::Max(FMath::Cos(Figure.StridePhase), 0.f) * Figure.RunAmount;
	const float LiftOther = FMath::Max(-FMath::Cos(Figure.StridePhase), 0.f) * Figure.RunAmount;
	const FVector FootL = Feet - Side * 14.f + MoveDir * Swing * 38.f + Up * (4.f + Lift * 16.f);
	const FVector FootR = Feet + Side * 14.f - MoveDir * Swing * 38.f + Up * (4.f + LiftOther * 16.f);

	FVector HandL = ShoulderL - Up * 62.f - Side * 6.f - MoveDir * Swing * 25.f;
	FVector HandR = ShoulderR - Up * 62.f + Side * 6.f + MoveDir * Swing * 25.f;
	switch (Pose)
	{
	case EPose::Idle:
		break;
	case EPose::Pass:
		HandL = Feet + Up * 70.f + Forward * 55.f - Side * 5.f;
		HandR = Feet + Up * 70.f + Forward * 55.f + Side * 5.f;
		break;
	case EPose::Set:
		HandL = Neck + Up * 48.f + Forward * 12.f - Side * 11.f;
		HandR = Neck + Up * 48.f + Forward * 12.f + Side * 11.f;
		break;
	case EPose::AttackWindUp:
		HandL = Neck + Up * 58.f + Forward * 18.f - Side * 18.f;
		HandR = Neck + Up * 25.f - Forward * 38.f + Side * 24.f;
		break;
	case EPose::AttackSwing:
		HandR = Neck + Forward * 62.f - Up * 18.f + Side * 10.f;
		break;
	case EPose::Block:
		HandL = Neck + Up * 66.f + Forward * 8.f - Side * 15.f;
		HandR = Neck + Up * 66.f + Forward * 8.f + Side * 15.f;
		break;
	case EPose::ServeToss:
		HandL = Neck + Up * 56.f + Forward * 22.f - Side * 12.f;
		HandR = Neck + Up * 22.f - Forward * 34.f + Side * 24.f;
		break;
	}

	const int32 Base = Player * 6;
	PlaceLimb(Base, Hip, Neck, 40.f);
	Limbs[Base + 1]->SetWorldLocation(Neck + Up * 24.f);
	PlaceLimb(Base + 2, ShoulderL, HandL, 12.f);
	PlaceLimb(Base + 3, ShoulderR, HandR, 12.f);
	PlaceLimb(Base + 4, Hip - Side * 11.f, FootL, 15.f);
	PlaceLimb(Base + 5, Hip + Side * 11.f, FootR, 15.f);
}

void AVC26Arena::SyncShapes(float DeltaSeconds)
{
	const auto& Players = Sim->players();
	for (int32 i = 0; i < 12; ++i)
	{
		AnimateFigure(i, DeltaSeconds);
	}

	BallShape->SetWorldLocation(ToWorld(Sim->ball().pos));

	const FVector Controlled = ToWorld(Players[Sim->controlled()].pos);
	ControlledMarker->SetVisibility(Sim->humanHome());
	ControlledMarker->SetWorldLocation(FVector(Controlled.X, Controlled.Y, 3.f));

	// Ground reticle (GDD 1.3): where the ball comes down, or where a serve is aimed.
	const vc26::PendingContact& Pending = Sim->pending();
	const bool bReceiving = Sim->humanHome() && Sim->hasLanding() && Pending.active && Pending.team == vc26::Team::Home;
	const bool bAiming = Sim->humanServing();
	const bool bBlocking = Sim->humanBlocking();
	LandingMarker->SetVisibility(bReceiving || bAiming || bBlocking);
	if (bBlocking)
	{
		// Marks the hitter's spot, so the player can line up on it.
		const FVector Spot = ToWorld(Sim->block().attackPoint);
		LandingMarker->SetWorldLocation(FVector(Spot.X, Spot.Y, 4.f));
	}
	else if (bReceiving)
	{
		const FVector Spot = ToWorld(Pending.point);
		LandingMarker->SetWorldLocation(FVector(Spot.X, Spot.Y, 4.f));
	}
	else if (bAiming)
	{
		LandingMarker->SetWorldLocation(ToWorld(vc26::Vec3{6.f, Sim->serveAim(), 0.04f}));
	}
}

void AVC26Arena::UpdateCamera(float DeltaSeconds)
{
	const FVector Focus = ToWorld(Sim->players()[Sim->controlled()].pos);
	const FVector Ball = ToWorld(Sim->ball().pos);

	// Behind the controlled player and looking past them towards the ball.
	FVector Desired(FMath::Clamp(Focus.X - 700.f, -1550.f, -1000.f), Focus.Y * 0.6f, 560.f);
	const FVector LookAt = FMath::Lerp(FVector(-50.f, Focus.Y * 0.5f, 70.f), Ball, 0.3f);
	const FRotator DesiredRotation = (LookAt - Desired).Rotation();

	if (!bCameraReady)
	{
		CameraPosition = Desired;
		Camera->SetWorldRotation(DesiredRotation);
		bCameraReady = true;
	}
	CameraPosition = FMath::VInterpTo(CameraPosition, Desired, DeltaSeconds, 3.f);
	Camera->SetWorldRotation(FMath::RInterpTo(Camera->GetComponentRotation(), DesiredRotation, DeltaSeconds, 4.f));

	ShakeLeft = FMath::Max(ShakeLeft - DeltaSeconds, 0.f);
	Camera->SetWorldLocation(CameraPosition + FMath::VRand() * ShakeLeft * 30.f);
}
