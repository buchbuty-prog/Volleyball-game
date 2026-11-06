#include "VC26Arena.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
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
	const FLinearColor LiberoJersey(0.95f, 0.7f, 0.05f);
	const FLinearColor ShoeColor(0.85f, 0.85f, 0.85f);
	const FLinearColor SkinTones[] = {
		FLinearColor(0.82f, 0.56f, 0.42f), FLinearColor(0.55f, 0.33f, 0.2f), FLinearColor(0.25f, 0.14f, 0.08f), FLinearColor(0.9f, 0.68f, 0.54f)};
	const FLinearColor HairColors[] = {
		FLinearColor(0.02f, 0.015f, 0.01f), FLinearColor(0.12f, 0.06f, 0.02f), FLinearColor(0.4f, 0.25f, 0.08f)};

	// Multiplied into the athlete texture's own skin, and added to its near-black hair.
	const FLinearColor AthleteSkin[] = {
		FLinearColor(1.f, 1.f, 1.f), FLinearColor(0.62f, 0.5f, 0.42f), FLinearColor(0.3f, 0.2f, 0.16f),
		FLinearColor(1.1f, 1.02f, 0.96f), FLinearColor(0.82f, 0.72f, 0.6f), FLinearColor(0.45f, 0.33f, 0.27f)};
	const FLinearColor AthleteHair[] = {
		FLinearColor(0.f, 0.f, 0.f), FLinearColor(0.1f, 0.05f, 0.02f), FLinearColor(0.3f, 0.17f, 0.06f),
		FLinearColor(0.02f, 0.01f, 0.01f), FLinearColor(0.55f, 0.4f, 0.16f)};

	enum EPart
	{
		Torso, Shorts, Head, Hair,
		UpperArmL, LowerArmL, UpperArmR, LowerArmR,
		ThighL, ShinL, ThighR, ShinR,
		ShoeL, ShoeR,
		NeckBone, HandL, HandR, PadL, PadR,
		PartCount
	};

	// The attack's hand peaks about 0.7 s into its clip, so a wind-up that starts
	// 0.55 s before contact begins at 0.15 s. Fade is how long the clip takes to blend in.
	struct FClipInfo
	{
		const TCHAR* Asset;
		float Start;
		float Rate;
		bool bLoop;
		float Fade;
	};
	const FClipInfo ClipInfo[] = {
		{TEXT("A_Idle"), 0.f, 1.f, true, 0.25f},
		{TEXT("A_Run"), 0.f, 1.f, true, 0.18f},
		{TEXT("A_Spike"), 0.15f, 1.f, false, 0.12f},
		{TEXT("A_Spike"), 0.62f, 1.f, false, 0.06f},
		{TEXT("A_Dive"), 0.f, 1.6f, false, 0.1f},
		{TEXT("A_Celebrate"), 0.f, 1.2f, true, 0.4f},
	};
	constexpr int32 CrowdStride = 5;       // parts per shirt colour in CrowdGroups
	constexpr float DiveTravel = 0.3f;     // the share of the dive animation's own travel that is kept
	constexpr float RunClipSpeed = 533.f;  // cm/s the run clip covers, so the feet match the floor
	constexpr float SwingSeconds = 0.28f;  // how long a touch is held after contact
	constexpr float LandSeconds = 0.3f;

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

UMaterialInstanceDynamic* AVC26Arena::ColorMaterial(const FLinearColor& Color)
{
	static UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	TObjectPtr<UMaterialInstanceDynamic>& Material = MaterialByColor.FindOrAdd(Color.ToFColor(false).ToPackedRGBA());
	if (!Material)
	{
		Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
	return Material;
}

UStaticMeshComponent* AVC26Arena::AddShape(UStaticMesh* Mesh, const FVector& CentreCm, const FVector& SizeCm, const FLinearColor& Color, bool bCastShadow, bool bStatic)
{
	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(this);
	Shape->SetMobility(EComponentMobility::Movable);
	Shape->SetStaticMesh(Mesh);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCastShadow(bCastShadow);
	Shape->SetAffectDistanceFieldLighting(bStatic);
	Shape->SetAffectDynamicIndirectLighting(bStatic);
	Shape->SetMaterial(0, ColorMaterial(Color));
	Shape->SetupAttachment(Root);
	Shape->RegisterComponent();
	Shape->SetWorldLocation(CentreCm);
	Shape->SetWorldScale3D(SizeCm / 100.f);  // the engine's basic shapes are 1 m across
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
	WallShapes.Add(AddShape(CubeMesh, FVector(HallHalfX, 0, HallHeight / 2.f), FVector(20.f, HallHalfY * 2.f, HallHeight), WallColor, false));
	WallShapes.Add(AddShape(CubeMesh, FVector(-HallHalfX, 0, HallHeight / 2.f), FVector(20.f, HallHalfY * 2.f, HallHeight), WallColor, false));
	WallShapes.Add(AddShape(CubeMesh, FVector(0, HallHalfY, HallHeight / 2.f), FVector(HallHalfX * 2.f, 20.f, HallHeight), WallColor, false));
	WallShapes.Add(AddShape(CubeMesh, FVector(0, -HallHalfY, HallHeight / 2.f), FVector(HallHalfX * 2.f, 20.f, HallHeight), WallColor, false));
	RoofShapes.Add(AddShape(CubeMesh, FVector(0, 0, HallHeight), FVector(HallHalfX * 2.f, HallHalfY * 2.f, 20.f), FLinearColor(0.03f, 0.03f, 0.05f), false));

	// Covers the hall floor for beach matches.
	Sand = AddShape(CubeMesh, FVector(0, 0, 1.9f), FVector(HallHalfX * 2.f, HallHalfY * 2.f, 0.2f), FLinearColor(0.62f, 0.43f, 0.2f), false);
	Sand->SetVisibility(false);

	// Roof trusses and light banks, for depth when the camera looks up.
	for (float X = -1500.f; X <= 1500.f; X += 500.f)
	{
		RoofShapes.Add(AddShape(CubeMesh, FVector(X, 0, HallHeight - 60.f), FVector(30.f, HallHalfY * 2.f, 40.f), FLinearColor(0.12f, 0.12f, 0.14f), false));
	}
	for (float X = -800.f; X <= 800.f; X += 400.f)
	{
		for (const float Y : {-400.f, 400.f})
		{
			RoofShapes.Add(AddShape(CubeMesh, FVector(X, Y, HallHeight - 110.f), FVector(240.f, 90.f, 12.f), FLinearColor(1.f, 0.98f, 0.92f), false));
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

void AVC26Arena::ApplyVenue(bool bBeach)
{
	Sand->SetVisibility(bBeach);
	for (UStaticMeshComponent* Shape : RoofShapes)
	{
		Shape->SetVisibility(!bBeach);
	}
	UMaterialInstanceDynamic* Wall = ColorMaterial(bBeach ? FLinearColor(0.35f, 0.62f, 0.9f) : FLinearColor(0.05f, 0.06f, 0.1f));
	for (UStaticMeshComponent* Shape : WallShapes)
	{
		Shape->SetMaterial(0, Wall);
	}
}

void AVC26Arena::BuildCourt()
{
	const float Length = vc26::kCourtHalfLength;
	const float Width = vc26::kCourtHalfWidth;
	const float LineWidth = 0.05f;

	// Indoor floor, hidden for beach matches along with the roof.
	RoofShapes.Add(AddShape(CubeMesh, FVector(0, 0, 0.5f), FVector(Length * 2.f + 8.f, Width * 2.f + 6.f, 0.01f) * MetresToCm, FLinearColor(0.03f, 0.3f, 0.42f), false));
	RoofShapes.Add(AddShape(CubeMesh, FVector(0, 0, 1.2f), FVector(Length * 2.f, Width * 2.f, 0.01f) * MetresToCm, FLinearColor(0.78f, 0.34f, 0.08f), false));
	RoofShapes.Add(AddShape(CubeMesh, FVector(0, 0, 1.6f), FVector(6.f, Width * 2.f, 0.01f) * MetresToCm, FLinearColor(0.86f, 0.42f, 0.1f), false));  // front zone tint
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
		AddShape(CubeMesh, FVector(0, Y, NetTop - 50.f), FVector(0.8f, 0.8f, 100.f), MeshColor, false, false);
	}
	for (float Z = NetTop - 100.f; Z < NetTop; Z += 12.5f)
	{
		AddShape(CubeMesh, FVector(0, 0, Z), FVector(0.8f, NetWidth, 0.8f), MeshColor, false, false);
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
			AddShape(CylinderMesh, FVector(0, Y, NetTop - 90.f + Band * 20.f + 10.f), FVector(2.f, 2.f, 20.f), Color, false, false);
		}
	}

	// Referee stand at the far post.
	RefereeSpot = FVector(0.f, PostY + 70.f, 120.f);
	AddShape(CubeMesh, RefereeSpot - FVector(0.f, 0.f, 60.f), FVector(60.f, 60.f, 120.f), FLinearColor(0.3f, 0.32f, 0.36f));
	RefereeShapes.Add(AddShape(CylinderMesh, RefereeSpot + FVector(0.f, 0.f, 45.f), FVector(32.f, 32.f, 90.f), FLinearColor(0.08f, 0.08f, 0.1f)));
	RefereeShapes.Add(AddShape(SphereMesh, RefereeSpot + FVector(0.f, 0.f, 105.f), FVector(24.f), SkinTones[0]));

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
	static const FLinearColor ShirtColors[] = {
		HomeJersey, AwayJersey, FLinearColor(0.85f, 0.85f, 0.85f),
		FLinearColor(0.9f, 0.65f, 0.05f), FLinearColor(0.07f, 0.07f, 0.09f), FLinearColor(0.1f, 0.45f, 0.25f)};
	constexpr int32 Rows = 7;

	auto AddInstances = [this](UStaticMesh* Mesh, const FLinearColor& Color) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* Group = NewObject<UInstancedStaticMeshComponent>(this);
		Group->SetMobility(EComponentMobility::Movable);
		Group->SetStaticMesh(Mesh);
		Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Group->SetCastShadow(false);
		Group->SetAffectDistanceFieldLighting(false);
		Group->SetAffectDynamicIndirectLighting(false);
		Group->SetMaterial(0, ColorMaterial(Color));
		Group->SetupAttachment(Root);
		Group->RegisterComponent();
		CrowdGroups.Add(Group);
		return Group;
	};

	// Each shirt colour has its own bodies, heads, hair and arms, so a group can
	// jump as one. They sit next to each other in CrowdGroups, CrowdStride apart.
	struct FFans
	{
		UInstancedStaticMeshComponent* Body;
		UInstancedStaticMeshComponent* Shoulders;
		UInstancedStaticMeshComponent* Head;
		UInstancedStaticMeshComponent* Hair;
		UInstancedStaticMeshComponent* Arms;
	};
	TArray<FFans> Fans;
	for (const FLinearColor& Color : ShirtColors)
	{
		const int32 Index = Fans.Num();
		FFans Group;
		Group.Body = AddInstances(SphereMesh, Color);
		Group.Shoulders = AddInstances(SphereMesh, Color);
		Group.Head = AddInstances(SphereMesh, SkinTones[Index % UE_ARRAY_COUNT(SkinTones)]);
		Group.Hair = AddInstances(SphereMesh, HairColors[Index % UE_ARRAY_COUNT(HairColors)]);
		Group.Arms = AddInstances(CylinderMesh, Color);
		Group.Arms->SetVisibility(false);  // they go up on big points
		CrowdArms.Add(Group.Arms);
		Fans.Add(Group);
	}

	const FRotator Facing = (-OutwardStep.GetSafeNormal2D()).Rotation();
	const FVector Side = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
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
			const FFans& Group = Fans[Random.RandRange(0, Fans.Num() - 1)];
			const FRotator Turn = Facing + FRotator(0.f, Random.FRandRange(-18.f, 18.f), 0.f);
			Group.Body->AddInstance(FTransform(Turn, Spot + FVector(0, 0, 36.f * Tall), FVector(0.3f, 0.38f, 0.62f * Tall)));
			Group.Shoulders->AddInstance(FTransform(Turn, Spot + FVector(0, 0, 60.f * Tall), FVector(0.25f, 0.46f, 0.2f)));
			Group.Head->AddInstance(FTransform(Turn, Spot + FVector(0, 0, 84.f * Tall), FVector(0.19f, 0.17f, 0.22f)));
			Group.Hair->AddInstance(FTransform(Turn, Spot + FVector(-2.f, 0, 89.f * Tall), FVector(0.2f, 0.18f, 0.16f)));
			for (const float Out : {-1.f, 1.f})
			{
				const FRotator Raised = Turn + FRotator(0.f, 0.f, Out * 12.f);
				Group.Arms->AddInstance(FTransform(Raised, Spot + Side * Out * 24.f + FVector(0, 0, 88.f * Tall), FVector(0.09f, 0.09f, 0.6f)));
			}
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
			if (Part == Head || Part == HandL || Part == HandR) Mesh = SphereMesh;
			if (Part == PadL || Part == PadR) Color = FLinearColor(0.08f, 0.08f, 0.1f);
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
			// Small parts sit inside bigger ones, so their shadows add nothing.
			const bool bCastShadow = Part != Hair && Part != NeckBone && Part != HandL && Part != HandR && Part != PadL && Part != PadR;
			Parts.Add(AddShape(Mesh, FVector::ZeroVector, FVector(100.f), Color, bCastShadow, false));
		}

		// These never change size.
		const float S = Figures[i].Height;
		const int32 Base = i * PartCount;
		Parts[Base + Head]->SetWorldScale3D(FVector(0.23f, 0.21f, 0.27f) * S);
		Parts[Base + Hair]->SetWorldScale3D(FVector(0.235f, 0.22f, 0.22f) * S);
		Parts[Base + HandL]->SetWorldScale3D(FVector(0.1f * S));
		Parts[Base + HandR]->SetWorldScale3D(FVector(0.1f * S));
		Parts[Base + ShoeL]->SetWorldScale3D(FVector(0.28f, 0.11f, 0.08f));
		Parts[Base + ShoeR]->SetWorldScale3D(FVector(0.28f, 0.11f, 0.08f));

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
	BuildAthletes();
	ControlledMarker = AddShape(CylinderMesh, FVector::ZeroVector, FVector(100.f, 100.f, 1.f), FLinearColor(1.f, 0.9f, 0.f), false, false);
	AwayMarker = AddShape(CylinderMesh, FVector::ZeroVector, FVector(100.f, 100.f, 1.f), FLinearColor(1.f, 0.35f, 0.3f), false, false);
	BuildRing(TargetRing, FLinearColor(0.95f, 0.95f, 0.95f));
	BuildRing(ReadyRing, FLinearColor(0.1f, 0.9f, 0.3f));
	BuildRing(TimingRing, FLinearColor(1.f, 0.75f, 0.05f));
}

void AVC26Arena::BuildRing(TArray<TObjectPtr<UStaticMeshComponent>>& Ring, const FLinearColor& Color)
{
	for (int32 i = 0; i < 32; ++i)
	{
		Ring.Add(AddShape(CubeMesh, FVector::ZeroVector, FVector(10.f), Color, false, false));
	}
}

void AVC26Arena::PlaceRing(const TArray<TObjectPtr<UStaticMeshComponent>>& Ring, const FVector& Centre, float Radius, bool bVisible)
{
	if (!bVisible && !Ring[0]->IsVisible())
	{
		return;  // already hidden
	}
	const float Arc = 2.f * PI / Ring.Num();
	for (int32 i = 0; i < Ring.Num(); ++i)
	{
		Ring[i]->SetVisibility(bVisible);
		if (bVisible)
		{
			const float Angle = i * Arc;
			Ring[i]->SetWorldLocationAndRotation(Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius, FRotator(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f));
			Ring[i]->SetWorldScale3D(FVector(Radius * Arc * 0.0078f, 0.07f, 0.01f));
		}
	}
}

bool AVC26Arena::BuildAthletes()
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Athlete/SK_Athlete.SK_Athlete"));
	UMaterialInterface* Kit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Athlete/M_AthleteTeam.M_AthleteTeam"));
	UMaterialInterface* Hair = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Athlete/M_AthleteHair.M_AthleteHair"));
	if (!Mesh || !Kit)
	{
		return false;
	}

	for (const FClipInfo& Info : ClipInfo)
	{
		UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Athlete/%s.%s"), Info.Asset, Info.Asset));
		if (!Anim)
		{
			return false;
		}
		ClipAnims.Add(Anim);
	}

	for (int32 i = 0; i < 12; ++i)
	{
		USkeletalMeshComponent* Athlete = NewObject<USkeletalMeshComponent>(this);
		Athlete->SetMobility(EComponentMobility::Movable);
		Athlete->SetSkeletalMeshAsset(Mesh);
		Athlete->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Athlete->SetAffectDistanceFieldLighting(false);
		Athlete->SetAffectDynamicIndirectLighting(false);
		Athlete->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		Athlete->SetupAttachment(Root);
		Athlete->RegisterComponent();
		Athlete->SetAnimInstanceClass(UVC26AthleteAnim::StaticClass());
		Athlete->SetWorldScale3D(FVector(Figures[i].Height));
		// One body, twelve people: their own skin, hair, shorts and number.
		UMaterialInstanceDynamic* Shirt = UMaterialInstanceDynamic::Create(Kit, this);
		Shirt->SetVectorParameterValue(TEXT("Team"), i < 6 ? HomeJersey : AwayJersey);
		Shirt->SetVectorParameterValue(TEXT("Shorts"), i < 6 ? HomeShorts : AwayShorts);
		Shirt->SetVectorParameterValue(TEXT("Print"), i < 6 ? HomeShorts : FLinearColor::White);
		Shirt->SetVectorParameterValue(TEXT("Skin"), AthleteSkin[(i * 5 + i / 6) % UE_ARRAY_COUNT(AthleteSkin)]);
		Shirt->SetScalarParameterValue(TEXT("Number"), i % 6 + 1);
		Athlete->SetMaterial(0, Shirt);
		if (Hair)
		{
			UMaterialInstanceDynamic* Head = UMaterialInstanceDynamic::Create(Hair, this);
			Head->SetVectorParameterValue(TEXT("Hair"), AthleteHair[(i * 7 + i / 6) % UE_ARRAY_COUNT(AthleteHair)]);
			Athlete->SetMaterial(1, Head);
		}
		Athletes.Add(Athlete);

		for (int32 Part = 0; Part < PartCount; ++Part)
		{
			Parts[i * PartCount + Part]->SetVisibility(false);
		}
		Numbers[i]->SetVisibility(false);  // the kit already carries a number
	}

	// The referee is the same body in plain black, on the stand.
	const FLinearColor Black(0.015f, 0.015f, 0.02f);
	Referee = NewObject<USkeletalMeshComponent>(this);
	Referee->SetMobility(EComponentMobility::Movable);
	Referee->SetSkeletalMeshAsset(Mesh);
	Referee->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Referee->SetAffectDistanceFieldLighting(false);
	Referee->SetAffectDynamicIndirectLighting(false);
	Referee->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Referee->SetupAttachment(Root);
	Referee->RegisterComponent();
	Referee->SetAnimInstanceClass(UVC26AthleteAnim::StaticClass());
	Referee->SetWorldLocationAndRotation(RefereeSpot, FRotator(0.f, 180.f, 0.f));  // +Y on the body faces the court
	UMaterialInstanceDynamic* Kit0 = UMaterialInstanceDynamic::Create(Kit, this);
	Kit0->SetVectorParameterValue(TEXT("Team"), Black);
	Kit0->SetVectorParameterValue(TEXT("Shorts"), Black);
	Kit0->SetVectorParameterValue(TEXT("Print"), Black);
	Kit0->SetVectorParameterValue(TEXT("Skin"), AthleteSkin[1]);
	Referee->SetMaterial(0, Kit0);
	RefereeDrive.Clip = ClipAnims[static_cast<int32>(EClip::Idle)];
	for (UStaticMeshComponent* Shape : RefereeShapes)
	{
		Shape->SetVisibility(false);
	}
	return true;
}

void AVC26Arena::AnimateReferee(float DeltaSeconds)
{
	if (!Referee)
	{
		return;
	}
	const float Length = RefereeDrive.Clip->GetPlayLength();
	RefereeDrive.Time = FMath::Fmod(RefereeDrive.Time + DeltaSeconds, Length);

	// An arm goes up to give the point, and the eyes follow the ball.
	const vc26::RallyPhase Phase = Sim->phase();
	const bool bSignal = Phase == vc26::RallyPhase::PointOver;
	RefereeDrive.Cheer = FMath::FInterpTo(RefereeDrive.Cheer, bSignal ? 1.f : 0.f, DeltaSeconds, 10.f);
	RefereeDrive.LookWeight = 0.9f;
	const FTransform& Body = Referee->GetComponentTransform();
	RefereeDrive.Look = Body.InverseTransformPosition(SimToWorld(Sim->ball().pos));
	if (UVC26AthleteAnim* Anim = Cast<UVC26AthleteAnim>(Referee->GetAnimInstance()))
	{
		Anim->Drive = RefereeDrive;
	}
}

void AVC26Arena::AnimateAthlete(int32 Player, EPose Pose, const FVector& Feet, const FVector& Forward, const FVector& MoveDir, float Speed, float DeltaSeconds)
{
	FFigure& Figure = Figures[Player];
	USkeletalMeshComponent* Athlete = Athletes[Player];
	const vc26::RallyPhase Phase = Sim->phase();
	const bool bRallyOver = Phase == vc26::RallyPhase::PointOver || Phase == vc26::RallyPhase::MatchOver;
	const bool bLost = bRallyOver && Pose != EPose::Celebrate;
	const float Running = Figure.RunAmount;

	EClip Want = Running > 0.25f ? EClip::Run : EClip::Idle;
	FVC26AthleteDrive Goal;
	Goal.LookWeight = 0.9f;
	switch (Pose)
	{
	case EPose::Idle:
		if (bLost)
		{
			Goal.Lean = 15.f;
			Goal.LookWeight = 0.f;
		}
		else if (!bRallyOver)
		{
			const float Still = 1.f - Running;
			Goal.Ready = 0.85f * Still;
			Goal.Crouch = 13.f * Still;
			Goal.Lean = 9.f * Still;
			Goal.Spread = 7.f * Still;
		}
		break;
	case EPose::Pass:
		Want = EClip::Idle;
		Goal.Crouch = 32.f;
		Goal.Lean = 34.f;
		Goal.Spread = 15.f;
		Goal.Platform = 1.f;
		break;
	case EPose::Set:
		Want = EClip::Idle;
		Goal.Crouch = 12.f;
		Goal.Spread = 6.f;
		Goal.Overhead = 1.f;
		break;
	case EPose::Block:
		Want = EClip::Idle;
		Goal.Block = 1.f;
		break;
	case EPose::AttackWindUp:
		Want = EClip::Spike;
		if (Sim->pending().active && Sim->pending().player == Player)
		{
			Goal.Hit = FMath::Clamp(1.f - Sim->pending().timeToContact / 0.16f, 0.f, 1.f);
		}
		break;
	case EPose::AttackSwing:
		// Follows through a wind-up already under way.
		Want = Figure.Clip == EClip::Spike ? EClip::Spike : EClip::Swing;
		Goal.Hit = FMath::Clamp(Figure.SwingLeft / SwingSeconds, 0.f, 1.f);
		Goal.LookWeight = 0.5f;
		break;
	case EPose::ServeToss:
		Want = EClip::Idle;
		Goal.Toss = 1.f;
		Goal.Crouch = 5.f;
		Goal.Spread = 9.f;
		break;
	case EPose::Dive:
		Want = EClip::Dive;
		Goal.LookWeight = 0.f;
		break;
	case EPose::Celebrate:
		if (Phase == vc26::RallyPhase::MatchOver)
		{
			Want = EClip::Samba;
			Goal.LookWeight = 0.f;
		}
		else
		{
			// A fist pump, each player on their own beat.
			Want = EClip::Idle;
			const float Pump = FMath::Abs(FMath::Sin(GetWorld()->GetTimeSeconds() * 6.f + Player));
			Goal.Cheer = 0.7f + 0.3f * Pump;
			Goal.Crouch = 8.f * Pump;
			Goal.LookWeight = 0.f;
		}
		break;
	}

	if (Want != Figure.Clip)
	{
		const int32 Index = static_cast<int32>(Want);
		const FClipInfo& Info = ClipInfo[Index];
		Figure.Clip = Want;
		Figure.FadeTime = Info.Fade;
		Figure.FadeLeft = Info.Fade;
		// Looping clips start at different points so a team never moves in unison.
		Figure.Drive.Time = Info.bLoop ? ClipAnims[Index]->GetPlayLength() * (Player % 6) / 6.f : Info.Start;
		Figure.Drive.Clip = ClipAnims[Index];
		Figure.Drive.bLoop = Info.bLoop;
		++Figure.Drive.Transition;
	}

	// The hips turn the way the player runs while the shoulders stay on the ball.
	// Going straight back is a backpedal: the same stride, played in reverse.
	const bool bRun = Want == EClip::Run;
	const bool bBackpedal = bRun && FVector::DotProduct(MoveDir, Forward) < -0.35f;
	const FVector BodyGoal = bRun && !bBackpedal && !MoveDir.IsNearlyZero() ? MoveDir : Forward;
	Figure.Body = (Figure.Body + (BodyGoal - Figure.Body) * FMath::Min(DeltaSeconds * 12.f, 1.f)).GetSafeNormal(UE_SMALL_NUMBER, Forward);

	// Clips run on the arena's clock, so they slow down with the replays.
	const FClipInfo& Info = ClipInfo[static_cast<int32>(Figure.Clip)];
	const float Length = Figure.Drive.Clip->GetPlayLength();
	float Rate = Info.Rate;
	if (bRun)
	{
		Rate = FMath::Clamp(Speed / (RunClipSpeed * Figure.Height), 0.4f, 1.4f) * (bBackpedal ? -0.8f : 1.f);
	}
	if (GalleryPose >= 0 && !Info.bLoop)
	{
		Figure.Drive.Time = 0.7f;  // the moment of the swing
		Rate = 0.f;
	}
	Figure.Drive.Time += DeltaSeconds * Rate;
	Figure.Drive.Time = Info.bLoop ? FMath::Fmod(Figure.Drive.Time + Length, Length) : FMath::Min(Figure.Drive.Time, Length);
	Figure.FadeLeft = FMath::Max(Figure.FadeLeft - DeltaSeconds, 0.f);
	Figure.Drive.Fade = Figure.FadeTime > 0.f ? 1.f - Figure.FadeLeft / Figure.FadeTime : 1.f;
	Figure.Drive.KeepTravel = Want == EClip::Dive ? DiveTravel : 0.f;

	// Mixamo characters face +Y.
	const FRotator Rotation = Figure.Body.Rotation() + FRotator(0.f, -90.f, 0.f);
	Athlete->SetWorldLocationAndRotation(Feet, Rotation);
	auto ToBody = [&](const FVector& World) { return Rotation.UnrotateVector(World - Feet) / Figure.Height; };

	// Legs fold in the air, less for a block, which stays long.
	const float Air = FMath::Clamp(Feet.Z / 35.f, 0.f, 1.f);
	Goal.Tuck = Air * (Pose == EPose::Block ? 0.25f : 0.8f);

	// Knees soak up the landing.
	const bool bAirborne = Feet.Z > 6.f;
	if (Figure.bAirborne && !bAirborne)
	{
		Figure.LandLeft = LandSeconds;
	}
	Figure.bAirborne = bAirborne;
	Figure.LandLeft = FMath::Max(Figure.LandLeft - DeltaSeconds, 0.f);
	const float Landing = Figure.LandLeft / LandSeconds;
	Goal.Crouch += 24.f * Landing;
	Goal.Lean += 12.f * Landing;

	const vc26::PendingContact& Pending = Sim->pending();
	if (Pending.active && Pending.player == Player)
	{
		Figure.Reach = SimToWorld(Pending.point);
	}
	else if (Pose == EPose::Block && Sim->block().active)
	{
		Figure.Reach = SimToWorld(Sim->block().attackPoint);
	}
	else if (Pose != EPose::AttackSwing)
	{
		Figure.Reach = SimToWorld(Sim->ball().pos);
	}

	FVC26AthleteDrive& Drive = Figure.Drive;
	Drive.Reach = ToBody(Figure.Reach);
	Drive.Look = ToBody(SimToWorld(Sim->ball().pos));
	Drive.Torso = Rotation.UnrotateVector(Forward);
	const auto Ease = [DeltaSeconds](float& Value, float Target, float Speed)
	{
		Value = FMath::FInterpTo(Value, Target, DeltaSeconds, Speed);
	};
	Ease(Drive.Crouch, Goal.Crouch, 14.f);
	Ease(Drive.Lean, Goal.Lean, 14.f);
	Ease(Drive.Spread, Goal.Spread, 14.f);
	Ease(Drive.Tuck, Goal.Tuck, 12.f);
	Ease(Drive.Ready, Goal.Ready, 10.f);
	Ease(Drive.Platform, Goal.Platform, 18.f);
	Ease(Drive.Overhead, Goal.Overhead, 16.f);
	Ease(Drive.Block, Goal.Block, 18.f);
	Ease(Drive.Toss, Goal.Toss, 12.f);
	Ease(Drive.Hit, Goal.Hit, 40.f);
	Ease(Drive.Cheer, Goal.Cheer, 16.f);
	Ease(Drive.LookWeight, Goal.LookWeight, 8.f);

	if (UVC26AthleteAnim* Anim = Cast<UVC26AthleteAnim>(Athlete->GetAnimInstance()))
	{
		Anim->Drive = Drive;
	}
}

void AVC26Arena::BuildBall()
{
	// Drawn a little larger than a real ball so it reads at a distance. The
	// three bands make its spin visible.
	BallShape = AddShape(SphereMesh, FVector::ZeroVector, FVector(32.f), FLinearColor(1.f, 0.82f, 0.08f), true, false);
	const FRotator Bands[] = {FRotator::ZeroRotator, FRotator(90.f, 0.f, 0.f), FRotator(0.f, 0.f, 90.f)};
	for (const FRotator& Rotation : Bands)
	{
		UStaticMeshComponent* Band = AddShape(SphereMesh, FVector::ZeroVector, FVector(32.f), FLinearColor(0.03f, 0.15f, 0.65f), false, false);
		Band->AttachToComponent(BallShape, FAttachmentTransformRules::KeepRelativeTransform);
		Band->SetRelativeLocation(FVector::ZeroVector);
		Band->SetRelativeRotation(Rotation);
		Band->SetRelativeScale3D(FVector(1.02f, 1.02f, 0.2f));
	}

	for (int32 i = 0; i < 7; ++i)
	{
		Trail.Add(AddShape(SphereMesh, FVector::ZeroVector, FVector(30.f - i * 3.5f), FLinearColor(1.f, 0.9f, 0.5f), false, false));
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
	if (GalleryPose >= 0)
	{
		return static_cast<EPose>(GalleryPose);
	}

	const vc26::BlockAttempt& Block = Sim->block();
	if (Block.active && (Block.player == Player || Block.partner == Player) && (Block.committed || Block.timeToContact < 0.6f))
	{
		return EPose::Block;
	}

	if (Sim->phase() == vc26::RallyPhase::PointOver || Sim->phase() == vc26::RallyPhase::MatchOver)
	{
		if (Sim->players()[Player].team == Sim->pointWinner())
		{
			return EPose::Celebrate;
		}
	}

	const vc26::PendingContact& Pending = Sim->pending();
	if (Pending.active && Pending.player == Player && Pending.timeToContact < 0.55f)
	{
		if (Pending.touch == 1)
		{
			return EPose::Pass;
		}
		return Pending.spike ? EPose::AttackWindUp : EPose::Set;
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
	// Bones keep their length in most poses, and a scale change costs a second transform update.
	const FVector Scale(Thickness / 100.f, Thickness / 100.f, FMath::Max(Along.Size(), 1.f) / 100.f);
	if (!Parts[Part]->GetRelativeScale3D().Equals(Scale, 0.005f))
	{
		Parts[Part]->SetWorldScale3D(Scale);
	}
}

void AVC26Arena::AnimateFigure(int32 Player, float DeltaSeconds)
{
	FFigure& Figure = Figures[Player];
	const int32 PlayerId = Sim->players()[Player].id;
	if (PlayerId != Figure.ShownId)
	{
		// The libero is the same body in a contrasting jersey.
		Figure.ShownId = PlayerId;
		const FLinearColor Jersey = PlayerId == 7 ? LiberoJersey : Player < 6 ? HomeJersey : AwayJersey;
		if (Athletes.IsEmpty())
		{
			for (const int32 Part : {Torso, UpperArmL, UpperArmR})
			{
				Parts[Player * PartCount + Part]->SetMaterial(0, ColorMaterial(Jersey));
			}
			Numbers[Player]->SetText(FText::AsNumber(PlayerId));
		}
		else if (UMaterialInstanceDynamic* Shirt = Cast<UMaterialInstanceDynamic>(Athletes[Player]->GetMaterial(0)))
		{
			Shirt->SetVectorParameterValue(TEXT("Team"), Jersey);
			Shirt->SetScalarParameterValue(TEXT("Number"), PlayerId);
		}
	}
	FVector Feet = SimToWorld(Sim->players()[Player].pos);
	const FVector Up = FVector::UpVector;
	const float S = Figure.Height;

	// Stride comes from how far the feet actually moved.
	FVector Moved = Feet - Figure.LastFeet;
	Moved.Z = 0.f;
	Figure.LastFeet = Feet;
	// From the sim, as frames in slow motion can land between its steps.
	FVector Velocity = SimToWorld(Sim->players()[Player].vel);
	Velocity.Z = 0.f;
	const float Speed = Velocity.Size();
	const FVector MoveDir = Velocity.GetSafeNormal();
	Figure.StridePhase += Moved.Size() * 0.035f;
	Figure.RunAmount = FMath::FInterpTo(Figure.RunAmount, FMath::Clamp(Speed / 500.f, 0.f, 1.f), DeltaSeconds, 12.f);
	Figure.SwingLeft = FMath::Max(Figure.SwingLeft - DeltaSeconds, 0.f);

	// Turn to the ball, or the net when it's right overhead.
	FVector ToBall = SimToWorld(Sim->ball().pos) - Feet;
	ToBall.Z = 0.f;
	const FVector NetDirection(Player < 6 ? 1.f : -1.f, 0.f, 0.f);
	const FVector WantedFacing = ToBall.Size() > 60.f ? ToBall.GetSafeNormal() : NetDirection;
	Figure.Facing = (Figure.Facing + (WantedFacing - Figure.Facing) * FMath::Min(DeltaSeconds * 10.f, 1.f)).GetSafeNormal(UE_SMALL_NUMBER, NetDirection);
	const EPose Pose = PoseFor(Player);
	const bool bDive = Pose == EPose::Dive;
	const FVector Forward = bDive ? Figure.DiveDirection : Figure.Facing;
	const FVector Side = FVector::CrossProduct(Up, Forward);
	if (!Athletes.IsEmpty())
	{
		AnimateAthlete(Player, Pose, Feet, Forward, MoveDir, Speed, DeltaSeconds);
		return;
	}
	if (Pose == EPose::Celebrate)
	{
		Feet += Up * FMath::Abs(FMath::Sin(GetWorld()->GetTimeSeconds() * 7.f + Player)) * 16.f;  // jumping for joy
	}

	// Ready stance: knees soft while waiting, deep for a pass.
	const float Crouch = (Pose == EPose::Pass ? 30.f : 6.f + Figure.RunAmount * 8.f) * S;
	const float Lean = (Pose == EPose::Pass ? 22.f : Figure.RunAmount * 14.f) * S;
	const float Breathe = FMath::Sin(Figure.StridePhase * 0.1f + GetWorld()->GetTimeSeconds() * 2.f + Player) * 1.2f;

	// A dive lays the body out towards the ball.
	const FVector Hip = bDive ? Feet + Up * 32.f + Forward * 25.f : Feet + Up * (94.f * S - Crouch + Breathe);
	const FVector Neck = bDive ? Hip + Forward * 56.f * S + Up * 10.f : Hip + Up * 58.f * S + Forward * Lean;
	const FVector ShoulderL = Neck - Side * 21.f * S - Up * 4.f;
	const FVector ShoulderR = Neck + Side * 21.f * S - Up * 4.f;
	const FVector HeadCentre = bDive ? Neck + Forward * 20.f * S + Up * 8.f : Neck + Up * 21.f * S + Forward * Lean * 0.2f;

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
	if (bDive)
	{
		FootL = Hip - Forward * 80.f - Side * 12.f + Up * 8.f;
		FootR = Hip - Forward * 74.f + Side * 14.f + Up * 22.f;
	}

	FVector LeftHand = ShoulderL - Up * 54.f * S - Side * 8.f + Forward * 6.f - MoveDir * Swing * 28.f;
	FVector RightHand = ShoulderR - Up * 54.f * S + Side * 8.f + Forward * 6.f + MoveDir * Swing * 28.f;
	FVector ElbowBend = -Forward - Up * 0.4f;
	switch (Pose)
	{
	case EPose::Idle:
		break;
	case EPose::Pass:
		LeftHand = Hip + Forward * 52.f * S - Side * 4.f + Up * 6.f;
		RightHand = Hip + Forward * 52.f * S + Side * 4.f + Up * 6.f;
		ElbowBend = -Up;
		break;
	case EPose::Set:
		LeftHand = HeadCentre + Up * 38.f * S + Forward * 14.f - Side * 11.f;
		RightHand = HeadCentre + Up * 38.f * S + Forward * 14.f + Side * 11.f;
		ElbowBend = Forward - Up;
		break;
	case EPose::AttackWindUp:
		LeftHand = HeadCentre + Up * 42.f * S + Forward * 20.f - Side * 16.f;
		RightHand = HeadCentre + Up * 12.f - Forward * 34.f + Side * 26.f;
		ElbowBend = -Forward + Up * 0.3f;
		break;
	case EPose::AttackSwing:
		RightHand = Neck + Forward * 58.f * S - Up * 14.f + Side * 10.f;
		ElbowBend = Up;
		break;
	case EPose::Dive:
		LeftHand = HeadCentre + Forward * 44.f - Side * 7.f;
		RightHand = HeadCentre + Forward * 44.f + Side * 7.f;
		ElbowBend = -Up;
		break;
	case EPose::Celebrate:
		LeftHand = HeadCentre + Up * 46.f * S - Side * 24.f;
		RightHand = HeadCentre + Up * 46.f * S + Side * 24.f;
		ElbowBend = -Forward;
		break;
	case EPose::Block:
		LeftHand = HeadCentre + Up * 52.f * S + Forward * 12.f - Side * 15.f;
		RightHand = HeadCentre + Up * 52.f * S + Forward * 12.f + Side * 15.f;
		ElbowBend = -Forward;
		break;
	case EPose::ServeToss:
		LeftHand = HeadCentre + Up * 40.f * S + Forward * 22.f - Side * 12.f;
		RightHand = HeadCentre + Up * 10.f - Forward * 32.f + Side * 26.f;
		ElbowBend = -Forward + Up * 0.3f;
		break;
	}

	const float ArmHalf = 29.f * S;
	const float LegHalf = 46.f * S;
	const FVector HipL = Hip - Side * 11.f * S;
	const FVector HipR = Hip + Side * 11.f * S;
	const FVector ElbowL = JointBetween(ShoulderL, LeftHand, ArmHalf, ElbowBend);
	const FVector ElbowR = JointBetween(ShoulderR, RightHand, ArmHalf, ElbowBend);
	const FVector KneeL = JointBetween(HipL, FootL, LegHalf, Forward);
	const FVector KneeR = JointBetween(HipR, FootR, LegHalf, Forward);

	const int32 Base = Player * PartCount;
	// The chest is wider than it is deep, so it follows the way the player faces.
	const FVector TorsoBottom = Hip + (Neck - Hip).GetSafeNormal() * 6.f;
	const FVector TorsoAlong = Neck - TorsoBottom;
	Parts[Base + Torso]->SetWorldLocationAndRotation((TorsoBottom + Neck) / 2.f, FRotationMatrix::MakeFromZX(TorsoAlong, Forward).Rotator());
	Parts[Base + Torso]->SetWorldScale3D(FVector(0.27f * S, 0.42f * S, TorsoAlong.Size() / 100.f));
	PlaceBone(Base + NeckBone, Neck, HeadCentre, 11.f * S);
	PlaceBone(Base + Shorts, Hip - Up * 16.f * S, Hip + Up * 10.f, 37.f * S);
	Parts[Base + Head]->SetWorldLocation(HeadCentre);
	Parts[Base + Hair]->SetWorldLocation(HeadCentre + Up * 5.f - Forward * 3.f);
	PlaceBone(Base + UpperArmL, ShoulderL, ElbowL, 12.f * S);
	PlaceBone(Base + LowerArmL, ElbowL, LeftHand, 9.f * S);
	PlaceBone(Base + UpperArmR, ShoulderR, ElbowR, 12.f * S);
	PlaceBone(Base + LowerArmR, ElbowR, RightHand, 9.f * S);
	PlaceBone(Base + ThighL, HipL, KneeL, 17.f * S);
	PlaceBone(Base + ShinL, KneeL, FootL, 12.f * S);
	PlaceBone(Base + ThighR, HipR, KneeR, 17.f * S);
	PlaceBone(Base + ShinR, KneeR, FootR, 12.f * S);
	PlaceBone(Base + PadL, KneeL + (HipL - KneeL).GetSafeNormal() * 7.f, KneeL + (FootL - KneeL).GetSafeNormal() * 8.f, 19.f * S);
	PlaceBone(Base + PadR, KneeR + (HipR - KneeR).GetSafeNormal() * 7.f, KneeR + (FootR - KneeR).GetSafeNormal() * 8.f, 19.f * S);
	Parts[Base + HandL]->SetWorldLocation(LeftHand);
	Parts[Base + HandR]->SetWorldLocation(RightHand);

	const FRotator FootRotation = Forward.Rotation();
	Parts[Base + ShoeL]->SetWorldLocationAndRotation(FootL + Forward * 7.f - Up * 2.f, FootRotation);
	Parts[Base + ShoeR]->SetWorldLocationAndRotation(FootR + Forward * 7.f - Up * 2.f, FootRotation);

	Numbers[Player]->SetWorldLocationAndRotation((Hip + Neck) / 2.f - Forward * 15.f * S, (-Forward).Rotation());
}

void AVC26Arena::UpdateBall(float DeltaSeconds)
{
	const FVector Ball = SimToWorld(Sim->ball().pos);
	const vc26::Vec3 Velocity = Sim->ball().vel;
	const float Speed = FMath::Sqrt(Velocity.x * Velocity.x + Velocity.y * Velocity.y + Velocity.z * Velocity.z);
	const bool bLive = Sim->phase() == vc26::RallyPhase::InPlay;

	BallSpin += Speed * DeltaSeconds * 90.f;
	BallShape->SetWorldLocationAndRotation(Ball, FRotator(BallSpin, BallSpin * 0.35f, 0.f));

	TrailPoints.Insert(Ball, 0);
	TrailPoints.SetNum(Trail.Num() * 2);
	const bool bShowTrail = bLive && Speed > 12.f;
	for (int32 i = 0; i < Trail.Num(); ++i)
	{
		Trail[i]->SetVisibility(bShowTrail);
		if (bShowTrail)
		{
			Trail[i]->SetWorldLocation(TrailPoints[i * 2 + 1]);
		}
	}
}

void AVC26Arena::SyncShapes(float DeltaSeconds)
{
	for (int32 i = 0; i < 12; ++i)
	{
		const bool bActive = Sim->playerActive(i);
		if (Figures[i].bShown != bActive)
		{
			Figures[i].bShown = bActive;
			if (!Athletes.IsEmpty())
			{
				Athletes[i]->SetVisibility(bActive, true);
			}
			else
			{
				for (int32 Part = 0; Part < PartCount; ++Part)
				{
					Parts[i * PartCount + Part]->SetVisibility(bActive);
				}
				Numbers[i]->SetVisibility(bActive);
			}
		}
		if (bActive)
		{
			AnimateFigure(i, DeltaSeconds * AnimationRate);
		}
	}
	AnimateReferee(DeltaSeconds);
	UpdateBall(DeltaSeconds);

	const bool bMatch = GetScreen() == EVC26Screen::Match && !IsReplaying();
	const FVector Controlled = SimToWorld(Sim->players()[Sim->controlled()].pos);
	ControlledMarker->SetVisibility(bMatch && Sim->humanHome());
	ControlledMarker->SetWorldLocation(FVector(Controlled.X, Controlled.Y, 3.f));
	const FVector Rival = SimToWorld(Sim->players()[Sim->controlled(vc26::Team::Away)].pos);
	AwayMarker->SetVisibility(bMatch && Sim->humanAway());
	AwayMarker->SetWorldLocation(FVector(Rival.X, Rival.Y, 3.f));

	UpdateRings();
	UpdateCrowd(DeltaSeconds);
}

void AVC26Arena::UpdateRings()
{
	constexpr float RingLead = 0.9f;  // seconds of warning the closing ring gives

	const vc26::PendingContact& Pending = Sim->pending();
	const vc26::BlockAttempt& Block = Sim->block();
	const bool bHuman = GetScreen() == EVC26Screen::Match && Sim->humanHome() && !IsReplaying();

	bool bShow = false;
	bool bReady = false;
	bool bTiming = false;
	FVector Centre = FVector::ZeroVector;
	float Radius = 100.f;
	float TimeToPress = 0.f;

	if (bHuman && Sim->humanBlocking())
	{
		bShow = true;
		bTiming = true;
		Centre = SimToWorld(Block.attackPoint);
		Radius = 70.f;
		TimeToPress = Block.timeToContact - Block.pressLead;
		bReady = FMath::Abs(Sim->players()[Block.player].pos.y - Block.attackPoint.y) <= Sim->blockReach(Block.team);
	}
	else if (bHuman && Sim->humanToPlay())
	{
		bShow = true;
		bTiming = !Pending.jumped;
		Centre = SimToWorld(Pending.point);
		Radius = Pending.touch == 1 ? 100.f : 70.f;
		TimeToPress = Pending.timeToContact - Pending.pressLead;
		const vc26::Vec3 Feet = Sim->players()[Pending.player].pos;
		bReady = Pending.touch > 1 || vc26::inCleanReach(Feet, Pending.point);
	}
	else if (bHuman && Sim->humanServing())
	{
		bShow = true;
		Centre = SimToWorld(vc26::Vec3{Sim->servingTeam() == vc26::Team::Home ? 6.f : -6.f, Sim->serveAim(), 0.f});
	}

	Centre.Z = 4.f;
	PlaceRing(TargetRing, Centre, Radius, bShow && !bReady);
	PlaceRing(ReadyRing, Centre, Radius, bShow && bReady);
	const float Closing = Radius * (1.f + FMath::Max(TimeToPress, 0.f) / RingLead * 2.5f);
	PlaceRing(TimingRing, Centre + FVector(0, 0, 1.f), Closing, bTiming && TimeToPress < RingLead && TimeToPress > -0.3f);
}

void AVC26Arena::UpdateCrowd(float DeltaSeconds)
{
	CrowdExcitement = FMath::Max(CrowdExcitement - DeltaSeconds * 0.45f, 0.f);
	if (CrowdExcitement <= 0.f && bCrowdSettled)
	{
		return;  // nothing to move during a rally
	}
	bCrowdSettled = CrowdExcitement <= 0.f;

	const float Time = GetWorld()->GetTimeSeconds();
	const bool bArmsUp = CrowdExcitement > 0.2f;
	if (bArmsUp != bCrowdArmsUp)
	{
		bCrowdArmsUp = bArmsUp;
		for (UInstancedStaticMeshComponent* Arms : CrowdArms)
		{
			Arms->SetVisibility(bArmsUp);
		}
	}
	for (int32 i = 0; i < CrowdGroups.Num(); ++i)
	{
		// One shirt colour's parts are neighbours in the list and move together.
		const float Phase = static_cast<float>(i / CrowdStride) * 1.3f;
		CrowdGroups[i]->SetRelativeLocation(FVector(0.f, 0.f, FMath::Abs(FMath::Sin(Time * 8.f + Phase)) * 26.f * CrowdExcitement));
	}
}

void AVC26Arena::UpdateCamera(float DeltaSeconds)
{
	FVector Desired;
	FVector LookAt;
	float Speed = 3.f;

	if (GalleryPose >= 0)
	{
		// The screenshot tour's close look at one player.
		const FVector Subject = SimToWorld(Sim->players()[GalleryPlayer].pos);
		Desired = Subject + GalleryView;
		LookAt = Subject + FVector(0.f, 0.f, 105.f);
		Speed = 20.f;
	}
	else if (IsReplaying())
	{
		// Low from the sideline, tracking the ball.
		const FVector Ball = SimToWorld(Sim->ball().pos);
		Desired = FVector(Ball.X * 0.75f, -780.f, 330.f);
		LookAt = FVector(Ball.X, Ball.Y * 0.6f, FMath::Clamp(Ball.Z * 0.6f, 60.f, 200.f));
		Speed = 4.f;
	}
	else if (GetScreen() != EVC26Screen::Match)
	{
		// A slow circle around the court behind the menus.
		OrbitAngle += DeltaSeconds * 0.08f;
		Desired = FVector(FMath::Cos(OrbitAngle) * 1250.f, FMath::Sin(OrbitAngle) * 780.f, 480.f);
		LookAt = FVector(0.f, 0.f, 120.f);
		Speed = 1.5f;
	}
	else
	{
		// A steady broadcast view from behind Home's end. It drifts gently with the
		// ball and ignores who is being controlled, so switching players never jolts it.
		const FVector Ball = SimToWorld(Sim->ball().pos);
		CameraSide = FMath::FInterpTo(CameraSide, Ball.Y * 0.35f, DeltaSeconds, 1.8f);
		Desired = FVector(-1480.f, CameraSide, 600.f);
		LookAt = FVector(-120.f, CameraSide * 0.6f, 60.f + FMath::Min(Ball.Z, 500.f) * 0.12f);
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
