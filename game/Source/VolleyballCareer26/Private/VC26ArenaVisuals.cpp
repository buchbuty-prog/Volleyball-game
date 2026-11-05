#include "VC26Arena.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	constexpr float MetresToCm = 100.f;
	constexpr float HallHalfX = 2100.f;
	constexpr float HallHalfY = 1500.f;
	constexpr float HallHeight = 1200.f;

	const FLinearColor HomeJersey(0.04f, 0.2f, 0.75f);
	const FLinearColor HomeShorts(0.9f, 0.9f, 0.92f);
	const FLinearColor AwayJersey(0.72f, 0.06f, 0.06f);
	const FLinearColor AwayShorts(0.05f, 0.05f, 0.06f);
	const FLinearColor ShoeColor(0.85f, 0.85f, 0.85f);
	const FLinearColor SkinTones[] = {
		FLinearColor(0.82f, 0.56f, 0.42f), FLinearColor(0.55f, 0.33f, 0.2f), FLinearColor(0.25f, 0.14f, 0.08f), FLinearColor(0.9f, 0.68f, 0.54f)};
	const FLinearColor HairColors[] = {
		FLinearColor(0.02f, 0.015f, 0.01f), FLinearColor(0.12f, 0.06f, 0.02f), FLinearColor(0.4f, 0.25f, 0.08f)};

	enum EPart
	{
		Torso, Shorts, Head, Hair,
		UpperArmL, LowerArmL, UpperArmR, LowerArmR,
		ThighL, ShinL, ThighR, ShinR,
		ShoeL, ShoeR,
		PartCount
	};

	// core/ is right-handed and Unreal is left-handed, so y flips. This keeps
	// Home's left on the left of the screen.
	FVector ToWorld(const vc26::Vec3& V)
	{
		return FVector(V.x, -V.y, V.z) * MetresToCm;
	}

	// Where the joint sits between two ends of a limb with equal halves.
	FVector JointBetween(const FVector& Start, FVector& End, float HalfLength, const FVector& BendTowards)
	{
		FVector Along = End - Start;
		const float Reach = FMath::Min(Along.Size(), HalfLength * 1.99f);
		Along = Along.GetSafeNormal();
		End = Start + Along * Reach;
		const float Offset = FMath::Sqrt(FMath::Max(HalfLength * HalfLength - Reach * Reach / 4.f, 0.f));
		const FVector Bend = (BendTowards - Along * FVector::DotProduct(BendTowards, Along)).GetSafeNormal();
		return Start + Along * (Reach / 2.f) + Bend * Offset;
	}
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

void AVC26Arena::AddBoard(const FVector& Centre, const FVector& Size, const FRotator& TextFacing)
{
	AddShape(CubeMesh, Centre, Size, FLinearColor(0.02f, 0.03f, 0.08f));

	UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this);
	Text->SetupAttachment(Root);
	Text->RegisterComponent();
	Text->SetText(FText::FromString(TEXT("VOLLEYBALL CAREER 26")));
	Text->SetHorizontalAlignment(EHTA_Center);
	Text->SetVerticalAlignment(EVRTA_TextCenter);
	Text->SetWorldSize(Size.Z * 0.62f);
	Text->SetTextRenderColor(FColor(255, 205, 40));
	Text->SetWorldRotation(TextFacing);
	Text->SetWorldLocation(Centre + TextFacing.Vector() * (FMath::Min(Size.X, Size.Y) / 2.f + 1.f));
}

void AVC26Arena::BuildHall()
{
	const FLinearColor WallColor(0.05f, 0.06f, 0.1f);
	AddShape(CubeMesh, FVector(0, 0, -10.f), FVector(HallHalfX * 2.f, HallHalfY * 2.f, 20.f), FLinearColor(0.02f, 0.16f, 0.24f), false);
	AddShape(CubeMesh, FVector(HallHalfX, 0, HallHeight / 2.f), FVector(20.f, HallHalfY * 2.f, HallHeight), WallColor, false);
	AddShape(CubeMesh, FVector(-HallHalfX, 0, HallHeight / 2.f), FVector(20.f, HallHalfY * 2.f, HallHeight), WallColor, false);
	AddShape(CubeMesh, FVector(0, HallHalfY, HallHeight / 2.f), FVector(HallHalfX * 2.f, 20.f, HallHeight), WallColor, false);
	AddShape(CubeMesh, FVector(0, -HallHalfY, HallHeight / 2.f), FVector(HallHalfX * 2.f, 20.f, HallHeight), WallColor, false);
	AddShape(CubeMesh, FVector(0, 0, HallHeight), FVector(HallHalfX * 2.f, HallHalfY * 2.f, 20.f), FLinearColor(0.03f, 0.03f, 0.05f), false);

	// Roof trusses and light banks, for depth when the camera looks up.
	for (float X = -1500.f; X <= 1500.f; X += 500.f)
	{
		AddShape(CubeMesh, FVector(X, 0, HallHeight - 60.f), FVector(30.f, HallHalfY * 2.f, 40.f), FLinearColor(0.12f, 0.12f, 0.14f), false);
	}
	for (float X = -800.f; X <= 800.f; X += 400.f)
	{
		for (const float Y : {-400.f, 400.f})
		{
			AddShape(CubeMesh, FVector(X, Y, HallHeight - 110.f), FVector(240.f, 90.f, 12.f), FLinearColor(1.f, 0.98f, 0.92f), false);
		}
	}

	// Stepped stands down both long sides and behind the far end.
	const FLinearColor StandColor(0.1f, 0.11f, 0.15f);
	for (int32 Row = 0; Row < 7; ++Row)
	{
		const float Rise = 30.f + Row * 48.f;
		const float Out = 880.f + Row * 75.f;
		for (const float Side : {-1.f, 1.f})
		{
			AddShape(CubeMesh, FVector(0, Side * Out, Rise / 2.f), FVector(HallHalfX * 1.7f, 80.f, Rise), StandColor, false);
		}
		AddShape(CubeMesh, FVector(1400.f + Row * 75.f, 0, Rise / 2.f), FVector(80.f, 1700.f, Rise), StandColor, false);
	}
}

void AVC26Arena::BuildCourt()
{
	const float Length = vc26::kCourtHalfLength;
	const float Width = vc26::kCourtHalfWidth;
	const float LineWidth = 0.05f;

	AddShape(CubeMesh, FVector(0, 0, 0.5f), FVector(Length * 2.f + 8.f, Width * 2.f + 6.f, 0.01f) * MetresToCm, FLinearColor(0.03f, 0.3f, 0.42f), false);
	AddShape(CubeMesh, FVector(0, 0, 1.2f), FVector(Length * 2.f, Width * 2.f, 0.01f) * MetresToCm, FLinearColor(0.78f, 0.34f, 0.08f), false);
	AddShape(CubeMesh, FVector(0, 0, 1.6f), FVector(6.f, Width * 2.f, 0.01f) * MetresToCm, FLinearColor(0.86f, 0.42f, 0.1f), false);  // front zone tint
	AddLine(-Length, 0.f, LineWidth, Width * 2.f);
	AddLine(Length, 0.f, LineWidth, Width * 2.f);
	AddLine(0.f, Width, Length * 2.f, LineWidth);
	AddLine(0.f, -Width, Length * 2.f, LineWidth);
	AddLine(0.f, 0.f, LineWidth, Width * 2.f);
	AddLine(-3.f, 0.f, LineWidth, Width * 2.f);
	AddLine(3.f, 0.f, LineWidth, Width * 2.f);

	// Net: an open mesh of thin strips, so the far team stays visible. The sim
	// treats everything below the tape as net too.
	const float NetTop = vc26::kNetHeight * MetresToCm;
	const float NetWidth = Width * 2.f * MetresToCm;
	const float PostY = (Width + 0.5f) * MetresToCm;
	const FLinearColor MeshColor(0.02f, 0.02f, 0.02f);
	for (float Y = -NetWidth / 2.f; Y <= NetWidth / 2.f; Y += 15.f)
	{
		AddShape(CubeMesh, FVector(0, Y, NetTop - 50.f), FVector(0.8f, 0.8f, 100.f), MeshColor, false);
	}
	for (float Z = NetTop - 100.f; Z < NetTop; Z += 12.5f)
	{
		AddShape(CubeMesh, FVector(0, 0, Z), FVector(0.8f, NetWidth, 0.8f), MeshColor, false);
	}
	AddShape(CubeMesh, FVector(0, 0, NetTop - 100.f), FVector(2.f, NetWidth, 5.f), FLinearColor::White, false);
	AddShape(CubeMesh, FVector(0, 0, NetTop - 3.5f), FVector(3.f, NetWidth, 7.f), FLinearColor::White, false);
	for (const float Y : {-PostY, PostY})
	{
		AddShape(CylinderMesh, FVector(0, Y, NetTop / 2.f + 10.f), FVector(11.f, 11.f, NetTop + 20.f), FLinearColor(0.75f, 0.75f, 0.8f));
		AddShape(CylinderMesh, FVector(0, Y, 90.f), FVector(32.f, 32.f, 180.f), FLinearColor(0.03f, 0.12f, 0.4f));  // post pad
	}
	for (const float Y : {-Width * MetresToCm, Width * MetresToCm})
	{
		for (int32 Band = 0; Band < 9; ++Band)
		{
			const FLinearColor Color = Band % 2 ? FLinearColor::White : FLinearColor(0.9f, 0.08f, 0.08f);
			AddShape(CylinderMesh, FVector(0, Y, NetTop - 90.f + Band * 20.f + 10.f), FVector(2.f, 2.f, 20.f), Color, false);
		}
	}

	// Referee stand at the far post.
	AddShape(CubeMesh, FVector(0, PostY + 70.f, 90.f), FVector(40.f, 40.f, 180.f), FLinearColor(0.3f, 0.32f, 0.36f));
	AddShape(CylinderMesh, FVector(0, PostY + 70.f, 225.f), FVector(32.f, 32.f, 90.f), FLinearColor(0.08f, 0.08f, 0.1f));
	AddShape(SphereMesh, FVector(0, PostY + 70.f, 285.f), FVector(24.f), SkinTones[0]);

	// LED boards around the court.
	const float BoardY = 690.f;
	for (const float X : {-850.f, 0.f, 850.f})
	{
		AddBoard(FVector(X, BoardY, 50.f), FVector(830.f, 12.f, 100.f), FRotator(0.f, -90.f, 0.f));
		AddBoard(FVector(X, -BoardY, 50.f), FVector(830.f, 12.f, 100.f), FRotator(0.f, 90.f, 0.f));
	}
	for (const float Y : {-430.f, 430.f})
	{
		AddBoard(FVector(1300.f, Y, 50.f), FVector(12.f, 840.f, 100.f), FRotator(0.f, 180.f, 0.f));
	}
}

void AVC26Arena::AddCrowdBlock(const FVector& Start, const FVector& AlongRow, const FVector& OutwardStep, int32 Seats)
{
	static UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static const FLinearColor ShirtColors[] = {
		FLinearColor(0.04f, 0.2f, 0.75f), FLinearColor(0.72f, 0.06f, 0.06f), FLinearColor(0.85f, 0.85f, 0.85f),
		FLinearColor(0.9f, 0.65f, 0.05f), FLinearColor(0.07f, 0.07f, 0.09f), FLinearColor(0.1f, 0.45f, 0.25f)};
	constexpr int32 Rows = 7;

	auto AddInstances = [this](UStaticMesh* Mesh, const FLinearColor& Color)
	{
		UInstancedStaticMeshComponent* Group = NewObject<UInstancedStaticMeshComponent>(this);
		Group->SetMobility(EComponentMobility::Movable);
		Group->SetStaticMesh(Mesh);
		Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Group->SetCastShadow(false);
		Group->SetupAttachment(Root);
		Group->RegisterComponent();
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Group->SetMaterial(0, Material);
		return Group;
	};

	TArray<UInstancedStaticMeshComponent*> Shirts;
	for (const FLinearColor& Color : ShirtColors)
	{
		Shirts.Add(AddInstances(CylinderMesh, Color));
	}
	UInstancedStaticMeshComponent* Heads = AddInstances(SphereMesh, SkinTones[0]);

	FRandomStream Random(static_cast<int32>(Start.X + Start.Y * 3.f));
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Seat = 0; Seat < Seats; ++Seat)
		{
			if (Random.FRand() < 0.14f)
			{
				continue;  // empty seat
			}
			const float Jitter = Random.FRandRange(-8.f, 8.f);
			const FVector Spot = Start + AlongRow * static_cast<float>(Seat) + OutwardStep * static_cast<float>(Row) + FVector(Jitter, Jitter, 0.f);
			const float Tall = Random.FRandRange(0.9f, 1.1f);
			Shirts[Random.RandRange(0, Shirts.Num() - 1)]->AddInstance(FTransform(FRotator::ZeroRotator, Spot + FVector(0, 0, 35.f * Tall), FVector(0.42f, 0.42f, 0.7f * Tall)));
			Heads->AddInstance(FTransform(FRotator::ZeroRotator, Spot + FVector(0, 0, 80.f * Tall), FVector(0.22f)));
		}
	}
}

void AVC26Arena::BuildCrowd()
{
	AddCrowdBlock(FVector(-1750.f, 880.f, 30.f), FVector(62.f, 0.f, 0.f), FVector(0.f, 75.f, 48.f), 56);
	AddCrowdBlock(FVector(-1750.f, -880.f, 30.f), FVector(62.f, 0.f, 0.f), FVector(0.f, -75.f, 48.f), 56);
	AddCrowdBlock(FVector(1400.f, -820.f, 30.f), FVector(0.f, 62.f, 0.f), FVector(75.f, 0.f, 48.f), 27);
}

void AVC26Arena::BuildPlayers()
{
	FRandomStream Random(26);
	for (int32 i = 0; i < 12; ++i)
	{
		const bool bHome = i < 6;
		const FLinearColor& Jersey = bHome ? HomeJersey : AwayJersey;
		const FLinearColor& ShortsColor = bHome ? HomeShorts : AwayShorts;
		const FLinearColor& Skin = SkinTones[Random.RandRange(0, UE_ARRAY_COUNT(SkinTones) - 1)];
		const FLinearColor& HairColor = HairColors[Random.RandRange(0, UE_ARRAY_COUNT(HairColors) - 1)];
		Figures[i].Height = Random.FRandRange(0.96f, 1.08f);

		for (int32 Part = 0; Part < PartCount; ++Part)
		{
			FLinearColor Color = Skin;
			UStaticMesh* Mesh = CylinderMesh;
			if (Part == Torso) Color = Jersey;
			if (Part == UpperArmL || Part == UpperArmR) Color = Jersey;
			if (Part == Shorts) Color = ShortsColor;
			if (Part == Head) Mesh = SphereMesh;
			if (Part == Hair)
			{
				Mesh = SphereMesh;
				Color = HairColor;
			}
			if (Part == ShoeL || Part == ShoeR)
			{
				Mesh = CubeMesh;
				Color = ShoeColor;
			}
			Parts.Add(AddShape(Mesh, FVector::ZeroVector, FVector(100.f), Color));
		}

		UTextRenderComponent* Number = NewObject<UTextRenderComponent>(this);
		Number->SetupAttachment(Root);
		Number->RegisterComponent();
		Number->SetText(FText::AsNumber(i % 6 + 1));
		Number->SetHorizontalAlignment(EHTA_Center);
		Number->SetVerticalAlignment(EVRTA_TextCenter);
		Number->SetWorldSize(30.f);
		Number->SetTextRenderColor(FColor::White);
		Numbers.Add(Number);
	}
	ControlledMarker = AddShape(CylinderMesh, FVector::ZeroVector, FVector(100.f, 100.f, 1.f), FLinearColor(1.f, 0.9f, 0.f), false);
	LandingMarker = AddShape(CylinderMesh, FVector::ZeroVector, FVector(240.f, 240.f, 1.f), FLinearColor(0.9f, 0.9f, 0.9f), false);
}

void AVC26Arena::BuildBall()
{
	// Drawn a little larger than a real ball so it reads at a distance. The
	// three bands make its spin visible.
	BallShape = AddShape(SphereMesh, FVector::ZeroVector, FVector(32.f), FLinearColor(1.f, 0.82f, 0.08f));
	const FRotator Bands[] = {FRotator::ZeroRotator, FRotator(90.f, 0.f, 0.f), FRotator(0.f, 0.f, 90.f)};
	for (const FRotator& Rotation : Bands)
	{
		UStaticMeshComponent* Band = AddShape(SphereMesh, FVector::ZeroVector, FVector(32.f), FLinearColor(0.03f, 0.15f, 0.65f), false);
		Band->AttachToComponent(BallShape, FAttachmentTransformRules::KeepRelativeTransform);
		Band->SetRelativeLocation(FVector::ZeroVector);
		Band->SetRelativeRotation(Rotation);
		Band->SetRelativeScale3D(FVector(1.02f, 1.02f, 0.2f));
	}

	for (int32 i = 0; i < 7; ++i)
	{
		Trail.Add(AddShape(SphereMesh, FVector::ZeroVector, FVector(30.f - i * 3.5f), FLinearColor(1.f, 0.9f, 0.5f), false));
	}
}

void AVC26Arena::BuildLights()
{
	auto AddSun = [this](const FRotator& Direction, float Intensity, bool bShadows)
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
	AddSun(FRotator(-72.f, 35.f, 0.f), 0.65f, true);
	AddSun(FRotator(-40.f, 200.f, 0.f), 0.15f, false);

	// Pools of light from the roof banks.
	for (float X = -800.f; X <= 800.f; X += 800.f)
	{
		for (const float Y : {-400.f, 400.f})
		{
			USpotLightComponent* Light = NewObject<USpotLightComponent>(this);
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetupAttachment(Root);
			Light->RegisterComponent();
			Light->SetWorldLocation(FVector(X, Y, HallHeight - 130.f));
			Light->SetWorldRotation(FRotator(-90.f, 0.f, 0.f));
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetIntensity(32.f);
			Light->SetAttenuationRadius(2600.f);
			Light->SetOuterConeAngle(62.f);
			Light->SetInnerConeAngle(30.f);
			Light->SetCastShadows(false);
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

void AVC26Arena::PlaceBone(int32 Part, const FVector& From, const FVector& To, float Thickness)
{
	const FVector Along = To - From;
	Parts[Part]->SetWorldLocationAndRotation((From + To) / 2.f, FRotationMatrix::MakeFromZ(Along).Rotator());
	Parts[Part]->SetWorldScale3D(FVector(Thickness / 100.f, Thickness / 100.f, FMath::Max(Along.Size(), 1.f) / 100.f));
}

void AVC26Arena::AnimateFigure(int32 Player, float DeltaSeconds)
{
	FFigure& Figure = Figures[Player];
	const FVector Feet = ToWorld(Sim->players()[Player].pos);
	const FVector Up = FVector::UpVector;
	const float S = Figure.Height;

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
	// Ready stance: knees soft while waiting, deep for a pass.
	const float Crouch = (Pose == EPose::Pass ? 30.f : 6.f + Figure.RunAmount * 8.f) * S;
	const float Lean = (Pose == EPose::Pass ? 22.f : Figure.RunAmount * 14.f) * S;
	const float Breathe = FMath::Sin(Figure.StridePhase * 0.1f + GetWorld()->GetTimeSeconds() * 2.f + Player) * 1.2f;

	const FVector Hip = Feet + Up * (94.f * S - Crouch + Breathe);
	const FVector Neck = Hip + Up * 58.f * S + Forward * Lean;
	const FVector ShoulderL = Neck - Side * 21.f * S - Up * 4.f;
	const FVector ShoulderR = Neck + Side * 21.f * S - Up * 4.f;
	const FVector HeadCentre = Neck + Up * 21.f * S + Forward * Lean * 0.2f;

	const float Swing = FMath::Sin(Figure.StridePhase) * Figure.RunAmount;
	const float Lift = FMath::Max(FMath::Cos(Figure.StridePhase), 0.f) * Figure.RunAmount;
	const float LiftOther = FMath::Max(-FMath::Cos(Figure.StridePhase), 0.f) * Figure.RunAmount;
	const bool bAirborne = Feet.Z > 5.f;
	FVector FootL = Feet - Side * 15.f * S + MoveDir * Swing * 42.f + Up * (5.f + Lift * 20.f);
	FVector FootR = Feet + Side * 15.f * S - MoveDir * Swing * 42.f + Up * (5.f + LiftOther * 20.f);
	if (bAirborne)
	{
		FootL += Up * 14.f - Forward * 10.f;
		FootR += Up * 22.f - Forward * 18.f;
	}

	FVector HandL = ShoulderL - Up * 54.f * S - Side * 8.f + Forward * 6.f - MoveDir * Swing * 28.f;
	FVector HandR = ShoulderR - Up * 54.f * S + Side * 8.f + Forward * 6.f + MoveDir * Swing * 28.f;
	FVector ElbowBend = -Forward - Up * 0.4f;
	switch (Pose)
	{
	case EPose::Idle:
		break;
	case EPose::Pass:
		HandL = Hip + Forward * 52.f * S - Side * 4.f + Up * 6.f;
		HandR = Hip + Forward * 52.f * S + Side * 4.f + Up * 6.f;
		ElbowBend = -Up;
		break;
	case EPose::Set:
		HandL = HeadCentre + Up * 38.f * S + Forward * 14.f - Side * 11.f;
		HandR = HeadCentre + Up * 38.f * S + Forward * 14.f + Side * 11.f;
		ElbowBend = Forward - Up;
		break;
	case EPose::AttackWindUp:
		HandL = HeadCentre + Up * 42.f * S + Forward * 20.f - Side * 16.f;
		HandR = HeadCentre + Up * 12.f - Forward * 34.f + Side * 26.f;
		ElbowBend = -Forward + Up * 0.3f;
		break;
	case EPose::AttackSwing:
		HandR = Neck + Forward * 58.f * S - Up * 14.f + Side * 10.f;
		ElbowBend = Up;
		break;
	case EPose::Block:
		HandL = HeadCentre + Up * 52.f * S + Forward * 12.f - Side * 15.f;
		HandR = HeadCentre + Up * 52.f * S + Forward * 12.f + Side * 15.f;
		ElbowBend = -Forward;
		break;
	case EPose::ServeToss:
		HandL = HeadCentre + Up * 40.f * S + Forward * 22.f - Side * 12.f;
		HandR = HeadCentre + Up * 10.f - Forward * 32.f + Side * 26.f;
		ElbowBend = -Forward + Up * 0.3f;
		break;
	}

	const float ArmHalf = 29.f * S;
	const float LegHalf = 46.f * S;
	const FVector HipL = Hip - Side * 11.f * S;
	const FVector HipR = Hip + Side * 11.f * S;
	const FVector ElbowL = JointBetween(ShoulderL, HandL, ArmHalf, ElbowBend);
	const FVector ElbowR = JointBetween(ShoulderR, HandR, ArmHalf, ElbowBend);
	const FVector KneeL = JointBetween(HipL, FootL, LegHalf, Forward);
	const FVector KneeR = JointBetween(HipR, FootR, LegHalf, Forward);

	const int32 Base = Player * PartCount;
	PlaceBone(Base + Torso, Hip + Up * 6.f, Neck, 36.f * S);
	PlaceBone(Base + Shorts, Hip - Up * 16.f * S, Hip + Up * 10.f, 37.f * S);
	Parts[Base + Head]->SetWorldLocation(HeadCentre);
	Parts[Base + Head]->SetWorldScale3D(FVector(0.23f, 0.21f, 0.27f) * S);
	Parts[Base + Hair]->SetWorldLocation(HeadCentre + Up * 5.f - Forward * 3.f);
	Parts[Base + Hair]->SetWorldScale3D(FVector(0.235f, 0.22f, 0.22f) * S);
	PlaceBone(Base + UpperArmL, ShoulderL, ElbowL, 12.f * S);
	PlaceBone(Base + LowerArmL, ElbowL, HandL, 9.f * S);
	PlaceBone(Base + UpperArmR, ShoulderR, ElbowR, 12.f * S);
	PlaceBone(Base + LowerArmR, ElbowR, HandR, 9.f * S);
	PlaceBone(Base + ThighL, HipL, KneeL, 17.f * S);
	PlaceBone(Base + ShinL, KneeL, FootL, 12.f * S);
	PlaceBone(Base + ThighR, HipR, KneeR, 17.f * S);
	PlaceBone(Base + ShinR, KneeR, FootR, 12.f * S);

	const FRotator FootRotation = Forward.Rotation();
	Parts[Base + ShoeL]->SetWorldLocationAndRotation(FootL + Forward * 7.f - Up * 2.f, FootRotation);
	Parts[Base + ShoeR]->SetWorldLocationAndRotation(FootR + Forward * 7.f - Up * 2.f, FootRotation);
	Parts[Base + ShoeL]->SetWorldScale3D(FVector(0.28f, 0.11f, 0.08f));
	Parts[Base + ShoeR]->SetWorldScale3D(FVector(0.28f, 0.11f, 0.08f));

	Numbers[Player]->SetWorldLocationAndRotation((Hip + Neck) / 2.f - Forward * 19.f * S, (-Forward).Rotation());
}

void AVC26Arena::UpdateBall(float DeltaSeconds)
{
	const FVector Ball = ToWorld(Sim->ball().pos);
	const vc26::Vec3 Velocity = Sim->ball().vel;
	const float Speed = FMath::Sqrt(Velocity.x * Velocity.x + Velocity.y * Velocity.y + Velocity.z * Velocity.z);
	const bool bLive = Sim->phase() == vc26::RallyPhase::InPlay;

	BallSpin += Speed * DeltaSeconds * 90.f;
	BallShape->SetWorldLocationAndRotation(Ball, FRotator(BallSpin, BallSpin * 0.35f, 0.f));

	TrailPoints.Insert(Ball, 0);
	TrailPoints.SetNum(Trail.Num() * 2);
	for (int32 i = 0; i < Trail.Num(); ++i)
	{
		Trail[i]->SetVisibility(bLive && Speed > 12.f);
		Trail[i]->SetWorldLocation(TrailPoints[i * 2 + 1]);
	}
}

void AVC26Arena::SyncShapes(float DeltaSeconds)
{
	for (int32 i = 0; i < 12; ++i)
	{
		AnimateFigure(i, DeltaSeconds);
	}
	UpdateBall(DeltaSeconds);

	const bool bMatch = GetScreen() == EVC26Screen::Match;
	const FVector Controlled = ToWorld(Sim->players()[Sim->controlled()].pos);
	ControlledMarker->SetVisibility(bMatch && Sim->humanHome());
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
	FVector Desired;
	FVector LookAt;
	float Speed = 3.f;

	if (GetScreen() != EVC26Screen::Match)
	{
		// A slow circle around the court behind the menus.
		OrbitAngle += DeltaSeconds * 0.08f;
		Desired = FVector(FMath::Cos(OrbitAngle) * 1250.f, FMath::Sin(OrbitAngle) * 780.f, 480.f);
		LookAt = FVector(0.f, 0.f, 120.f);
		Speed = 1.5f;
	}
	else
	{
		const FVector Focus = ToWorld(Sim->players()[Sim->controlled()].pos);
		const FVector Ball = ToWorld(Sim->ball().pos);
		// Behind the controlled player and looking past them towards the ball.
		Desired = FVector(FMath::Clamp(Focus.X - 700.f, -1550.f, -1000.f), Focus.Y * 0.6f, 560.f);
		LookAt = FMath::Lerp(FVector(-50.f, Focus.Y * 0.5f, 70.f), Ball, 0.3f);
	}

	const FRotator DesiredRotation = (LookAt - Desired).Rotation();
	if (!bCameraReady)
	{
		CameraPosition = Desired;
		Camera->SetWorldRotation(DesiredRotation);
		bCameraReady = true;
	}
	CameraPosition = FMath::VInterpTo(CameraPosition, Desired, DeltaSeconds, Speed);
	Camera->SetWorldRotation(FMath::RInterpTo(Camera->GetComponentRotation(), DesiredRotation, DeltaSeconds, 4.f));

	ShakeLeft = FMath::Max(ShakeLeft - DeltaSeconds, 0.f);
	Camera->SetWorldLocation(CameraPosition + FMath::VRand() * ShakeLeft * 30.f);
}
