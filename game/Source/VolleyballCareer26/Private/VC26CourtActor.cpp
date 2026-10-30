#include "VC26CourtActor.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "VC26TeamComponent.h"

namespace
{
// Court dimensions in centimetres (FIVB): 18 m x 9 m, net 2.43 m (men).
constexpr float CourtLength = 1800.f;
constexpr float CourtWidth = 900.f;
constexpr float HalfLength = CourtLength / 2.f;
constexpr float HalfWidth = CourtWidth / 2.f;
constexpr float AttackLine = 300.f;
constexpr float NetTop = 243.f;
constexpr float LineWidth = 5.f;

// Where each slot stands at serve receive: lateral from the team's own left
// sideline, depth from the centre line. Index = EVC26Slot (P1..P6).
const FVector2D SlotSpots[6] = {
	{750.f, 700.f}, // P1 right back
	{750.f, 250.f}, // P2 right front
	{450.f, 250.f}, // P3 middle front
	{150.f, 250.f}, // P4 left front
	{150.f, 700.f}, // P5 left back
	{450.f, 700.f}, // P6 middle back
};
const FVector2D ServeSpot = {750.f, 980.f};

const TCHAR* SlotNames[6] = {TEXT("P1"), TEXT("P2"), TEXT("P3"), TEXT("P4"), TEXT("P5"), TEXT("P6")};

const FLinearColor HomeColor(0.05f, 0.25f, 0.75f);
const FLinearColor AwayColor(0.75f, 0.08f, 0.08f);
const FLinearColor LiberoColor(0.95f, 0.75f, 0.05f); // liberos wear a contrasting jersey
const FLinearColor SkinColor(0.8f, 0.6f, 0.45f);

struct FRosterEntry
{
	int32 Number;
	const TCHAR* Name;
	EVC26Role Role;
};

// Standard 5-1 in slot order P1..P6, plus the libero last.
const FRosterEntry HomeRoster[7] = {
	{7, TEXT("Rivas"), EVC26Role::Setter},
	{11, TEXT("Okafor"), EVC26Role::OutsideHitter},
	{14, TEXT("Lindqvist"), EVC26Role::MiddleBlocker},
	{9, TEXT("Tanaka"), EVC26Role::Opposite},
	{4, TEXT("Moreau"), EVC26Role::OutsideHitter},
	{16, TEXT("Petrov"), EVC26Role::MiddleBlocker},
	{1, TEXT("Silva"), EVC26Role::Libero},
};
const FRosterEntry AwayRoster[7] = {
	{3, TEXT("Novak"), EVC26Role::Setter},
	{10, TEXT("Haddad"), EVC26Role::OutsideHitter},
	{12, TEXT("Kowalski"), EVC26Role::MiddleBlocker},
	{8, TEXT("Duarte"), EVC26Role::Opposite},
	{5, TEXT("Weber"), EVC26Role::OutsideHitter},
	{15, TEXT("Mensah"), EVC26Role::MiddleBlocker},
	{2, TEXT("Ito"), EVC26Role::Libero},
};

int32 PlayerId(bool bHome, int32 RosterIndex)
{
	return (bHome ? 1 : 11) + RosterIndex;
}

FVC26Lineup MakeLineup(bool bHome)
{
	const FRosterEntry* Roster = bHome ? HomeRoster : AwayRoster;
	FVC26Lineup Lineup;
	for (int32 i = 0; i < 6; ++i)
	{
		FVC26Player P;
		P.Id = PlayerId(bHome, i);
		P.Role = Roster[i].Role;
		Lineup.Starters.Add(P);
	}
	Lineup.bHasLibero = true;
	Lineup.Libero.Id = PlayerId(bHome, 6);
	Lineup.Libero.Role = EVC26Role::Libero;
	Lineup.bLiberoStarts = true;
	Lineup.LiberoStartSlot = EVC26Slot::P6; // in for the second middle
	Lineup.LiberoTargets = {PlayerId(bHome, 2), PlayerId(bHome, 5)};
	return Lineup;
}
}  // namespace

AVC26CourtActor::AVC26CourtActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	HomeTeam = CreateDefaultSubobject<UVC26TeamComponent>(TEXT("HomeTeam"));
	AwayTeam = CreateDefaultSubobject<UVC26TeamComponent>(TEXT("AwayTeam"));
	Match = CreateDefaultSubobject<UVC26MatchComponent>(TEXT("Match"));
}

void AVC26CourtActor::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (bSnapToGround)
	{
		const FVector Here = GetActorLocation();
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("VC26CourtGround"), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Here + FVector(0, 0, 100000.f), Here - FVector(0, 0, 100000.f), ECC_Visibility, Params))
		{
			SetActorLocation(Hit.ImpactPoint + FVector(0, 0, 5.f));
		}
	}

	BuildCourt();
	BuildTeam(true);
	BuildTeam(false);

	HomeTeam->OnLiberoSwap.AddDynamic(this, &AVC26CourtActor::HandleHomeLiberoSwap);
	AwayTeam->OnLiberoSwap.AddDynamic(this, &AVC26CourtActor::HandleAwayLiberoSwap);
	Match->OnSetWon.AddDynamic(this, &AVC26CourtActor::HandleSetWon);
	Match->OnMatchWon.AddDynamic(this, &AVC26CourtActor::HandleMatchWon);
	Match->OnMatchPoint.AddDynamic(this, &AVC26CourtActor::HandleMatchPoint);
	Match->OnSwitchSides.AddDynamic(this, &AVC26CourtActor::HandleSwitchSides);

	// Broadcast camera: high behind the home end line, looking down the court.
	const FVector CamLocation = GetActorLocation() + FVector(-HalfLength - 1100.f, 0.f, 1100.f);
	const FVector LookAt = GetActorLocation() + FVector(150.f, 0.f, 0.f);
	Camera = GetWorld()->SpawnActor<ACameraActor>(CamLocation, (LookAt - CamLocation).Rotation());
	if (Camera)
	{
		Camera->GetCameraComponent()->SetFieldOfView(65.f);
	}

	StartMatch();
}

UStaticMeshComponent* AVC26CourtActor::AddShape(UStaticMesh* Mesh, const FVector& Center, const FVector& SizeCm, const FLinearColor& Color)
{
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetupAttachment(RootComponent);
	Comp->RegisterComponent();
	Comp->SetRelativeLocation(Center);
	Comp->SetRelativeScale3D(SizeCm / 100.f); // engine basic shapes are 100 cm
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Mat->SetVectorParameterValue(TEXT("Color"), Color);
		Comp->SetMaterial(0, Mat);
	}
	SpawnedComponents.Add(Comp);
	return Comp;
}

void AVC26CourtActor::BuildCourt()
{
	const FLinearColor FreeZone(0.75f, 0.28f, 0.08f);
	const FLinearColor Playing(0.08f, 0.28f, 0.5f);
	const FLinearColor Line(0.95f, 0.95f, 0.95f);
	const FLinearColor NetColor(0.03f, 0.03f, 0.03f);

	AddShape(CubeMesh, FVector(0, 0, 1.f), FVector(CourtLength + 1200.f, CourtWidth + 1000.f, 2.f), FreeZone);
	AddShape(CubeMesh, FVector(0, 0, 2.f), FVector(CourtLength, CourtWidth, 2.f), Playing);

	const float LineZ = 3.5f;
	for (float Y : {-HalfWidth, HalfWidth}) // sidelines
	{
		AddShape(CubeMesh, FVector(0, Y, LineZ), FVector(CourtLength + LineWidth, LineWidth, 1.f), Line);
	}
	for (float X : {-HalfLength, 0.f, HalfLength, -AttackLine, AttackLine}) // end, centre and attack lines
	{
		AddShape(CubeMesh, FVector(X, 0, LineZ), FVector(LineWidth, CourtWidth, 1.f), Line);
	}

	// Net: posts 1 m outside the sidelines, 1 m tall mesh band with a white top tape.
	for (float Y : {-HalfWidth - 100.f, HalfWidth + 100.f})
	{
		AddShape(CylinderMesh, FVector(0, Y, NetTop / 2.f + 10.f), FVector(10.f, 10.f, NetTop + 20.f), FLinearColor(0.6f, 0.6f, 0.6f));
	}
	AddShape(CubeMesh, FVector(0, 0, NetTop - 50.f), FVector(2.f, CourtWidth + 200.f, 100.f), NetColor);
	AddShape(CubeMesh, FVector(0, 0, NetTop - 3.f), FVector(3.f, CourtWidth + 200.f, 7.f), Line);
}

void AVC26CourtActor::BuildTeam(bool bHome)
{
	const FRosterEntry* Roster = bHome ? HomeRoster : AwayRoster;
	for (int32 i = 0; i < 7; ++i)
	{
		FPlayerVisual V;
		V.Id = PlayerId(bHome, i);
		V.Number = Roster[i].Number;
		V.Name = Roster[i].Name;
		V.Role = Roster[i].Role;
		V.bHome = bHome;

		const FLinearColor Jersey = V.Role == EVC26Role::Libero ? LiberoColor : (bHome ? HomeColor : AwayColor);
		V.Body = AddShape(CylinderMesh, FVector::ZeroVector, FVector(50.f, 50.f, 170.f), Jersey);
		V.Head = AddShape(SphereMesh, FVector::ZeroVector, FVector(28.f), SkinColor);

		V.Label = NewObject<UTextRenderComponent>(this);
		V.Label->SetupAttachment(RootComponent);
		V.Label->RegisterComponent();
		V.Label->SetHorizontalAlignment(EHTA_Center);
		V.Label->SetWorldSize(32.f);
		V.Label->SetTextRenderColor(FColor::White);
		SpawnedComponents.Add(V.Label);

		Players.Add(V);
	}
}

void AVC26CourtActor::StartMatch()
{
	Match->StartMatch(false);
	HomeTeam->StartSet(MakeLineup(true), Mode, bHomeServesFirst);
	AwayTeam->StartSet(MakeLineup(false), Mode, !bHomeServesFirst);
	LastEvent = FString::Printf(TEXT("New match. %s serves first."), bHomeServesFirst ? TEXT("Home") : TEXT("Away"));
	UpdateTargets(true);
}

void AVC26CourtActor::PlayRally(bool bHomeWins)
{
	if (Match->IsFinished())
	{
		return;
	}
	const bool bHomeWasServing = HomeTeam->IsServing();
	LastEvent = bHomeWins ? (bHomeWasServing ? TEXT("Home point on serve.") : TEXT("Home side-out: home rotates."))
	                      : (bHomeWasServing ? TEXT("Away side-out: away rotates.") : TEXT("Away point on serve."));

	HomeTeam->RallyEnd(bHomeWins);
	AwayTeam->RallyEnd(!bHomeWins);
	Match->AwardPoint(bHomeWins ? EVC26Team::Home : EVC26Team::Away);
	UpdateTargets(false);
}

FVector AVC26CourtActor::SlotPosition(bool bHome, EVC26Slot Slot, bool bServing) const
{
	const FVector2D Spot = (bServing && Slot == EVC26Slot::P1) ? ServeSpot : SlotSpots[static_cast<int32>(Slot)];
	// Home faces +X (its left is -Y); away faces -X (its left is +Y).
	return bHome ? FVector(-Spot.Y, -HalfWidth + Spot.X, 0.f) : FVector(Spot.Y, HalfWidth - Spot.X, 0.f);
}

FVector AVC26CourtActor::BenchPosition(bool bHome, int32 BenchIndex) const
{
	const float X = (bHome ? -1.f : 1.f) * (250.f + 90.f * BenchIndex);
	return FVector(X, -HalfWidth - 350.f, 0.f);
}

void AVC26CourtActor::UpdateTargets(bool bSnap)
{
	int32 Bench[2] = {0, 0};
	for (FPlayerVisual& V : Players)
	{
		UVC26TeamComponent* Team = V.bHome ? HomeTeam : AwayTeam;
		EVC26Slot Slot = EVC26Slot::P1;
		const bool bOnCourt = Team->GetSlotOf(V.Id, Slot);
		V.Target = bOnCourt ? SlotPosition(V.bHome, Slot, Team->IsServing()) : BenchPosition(V.bHome, Bench[V.bHome ? 0 : 1]++);

		FString Text = FString::Printf(TEXT("%d %s"), V.Number, *V.Name);
		if (bOnCourt)
		{
			const int32 Fit = FMath::RoundToInt(Team->GetRoleFit(V.Id) * 100.f);
			Text += FString::Printf(TEXT(" | %s | Fit %d%%"), SlotNames[static_cast<int32>(Slot)], Fit);
		}
		else
		{
			Text += TEXT(" | bench");
		}
		V.Label->SetText(FText::FromString(Text));

		if (bSnap)
		{
			V.Body->SetRelativeLocation(V.Target + FVector(0, 0, 87.f));
		}
	}
}

void AVC26CourtActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && Camera)
	{
		if (PC->GetViewTarget() != Camera)
		{
			PC->bAutoManageActiveCameraTarget = false;
			PC->SetViewTarget(Camera);
		}
		if (PC->WasInputKeyJustPressed(EKeys::One))
		{
			PlayRally(true);
		}
		else if (PC->WasInputKeyJustPressed(EKeys::Two))
		{
			PlayRally(false);
		}
		else if (PC->WasInputKeyJustPressed(EKeys::R))
		{
			StartMatch();
		}
	}

	const FVector CamWorld = Camera ? Camera->GetActorLocation() : FVector::ZeroVector;
	for (FPlayerVisual& V : Players)
	{
		const FVector Current = V.Body->GetRelativeLocation();
		const FVector Next = FMath::VInterpTo(Current, V.Target + FVector(0, 0, 87.f), DeltaSeconds, 3.f);
		V.Body->SetRelativeLocation(Next);
		V.Head->SetRelativeLocation(Next + FVector(0, 0, 100.f));
		V.Label->SetRelativeLocation(Next + FVector(0, 0, 140.f));
		if (Camera)
		{
			// Keep names readable from the broadcast camera.
			FRotator Face = (CamWorld - V.Label->GetComponentLocation()).Rotation();
			Face.Pitch = 0.f;
			V.Label->SetWorldRotation(Face);
		}
	}

	if (GEngine)
	{
		const int32 HomePts = Match->GetPoints(EVC26Team::Home);
		const int32 AwayPts = Match->GetPoints(EVC26Team::Away);
		GEngine->AddOnScreenDebugMessage(1, 0.f, FColor::White,
			FString::Printf(TEXT("HOME %d  (sets %d)    -    AWAY %d  (sets %d)    Set %d    Serving: %s"),
				HomePts, Match->GetSets(EVC26Team::Home), AwayPts, Match->GetSets(EVC26Team::Away),
				Match->GetSetNumber(), HomeTeam->IsServing() ? TEXT("HOME") : TEXT("AWAY")));
		GEngine->AddOnScreenDebugMessage(2, 0.f, FColor::Yellow, LastEvent);
		GEngine->AddOnScreenDebugMessage(3, 0.f, FColor(180, 180, 180),
			TEXT("Keys: 1 = home wins rally   2 = away wins rally   R = restart   (click the game view first)"));
	}
}

const AVC26CourtActor::FPlayerVisual* AVC26CourtActor::FindPlayer(int32 Id) const
{
	return Players.FindByPredicate([Id](const FPlayerVisual& V) { return V.Id == Id; });
}

void AVC26CourtActor::LogLiberoSwap(bool bHome, EVC26Slot Slot, int32 LiberoId, int32 OtherPlayerId, bool bEntering)
{
	const FPlayerVisual* Libero = FindPlayer(LiberoId);
	const FPlayerVisual* Other = FindPlayer(OtherPlayerId);
	if (!Libero || !Other)
	{
		return;
	}
	const TCHAR* TeamName = bHome ? TEXT("Home") : TEXT("Away");
	const TCHAR* SlotName = SlotNames[static_cast<int32>(Slot)];
	if (bEntering)
	{
		LastEvent += FString::Printf(TEXT("  %s libero %s in for %s at %s."), TeamName, *Libero->Name, *Other->Name, SlotName);
	}
	else
	{
		LastEvent += FString::Printf(TEXT("  %s libero %s reaches %s and leaves; %s returns."), TeamName, *Libero->Name, SlotName, *Other->Name);
	}
}

void AVC26CourtActor::HandleHomeLiberoSwap(EVC26Slot Slot, int32 LiberoId, int32 OtherPlayerId, bool bEntering)
{
	LogLiberoSwap(true, Slot, LiberoId, OtherPlayerId, bEntering);
}

void AVC26CourtActor::HandleAwayLiberoSwap(EVC26Slot Slot, int32 LiberoId, int32 OtherPlayerId, bool bEntering)
{
	LogLiberoSwap(false, Slot, LiberoId, OtherPlayerId, bEntering);
}

void AVC26CourtActor::HandleSetWon(EVC26Team Team)
{
	LastEvent += FString::Printf(TEXT("  SET to %s!"), Team == EVC26Team::Home ? TEXT("HOME") : TEXT("AWAY"));
}

void AVC26CourtActor::HandleMatchWon(EVC26Team Team)
{
	LastEvent += FString::Printf(TEXT("  MATCH to %s! Press R to play again."), Team == EVC26Team::Home ? TEXT("HOME") : TEXT("AWAY"));
}

void AVC26CourtActor::HandleMatchPoint(EVC26Team Team)
{
	LastEvent += FString::Printf(TEXT("  Match point %s."), Team == EVC26Team::Home ? TEXT("HOME") : TEXT("AWAY"));
}

void AVC26CourtActor::HandleSwitchSides()
{
	LastEvent += TEXT("  Teams switch sides.");
}
