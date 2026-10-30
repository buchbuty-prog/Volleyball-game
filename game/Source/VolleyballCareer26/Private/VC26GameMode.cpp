#include "VC26GameMode.h"

#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "VC26CourtActor.h"

AVC26GameMode::AVC26GameMode()
{
	// The court actor owns the camera, so the player needs no visible pawn.
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void AVC26GameMode::StartPlay()
{
	Super::StartPlay();

	if (!TActorIterator<AVC26CourtActor>(GetWorld()))
	{
		GetWorld()->SpawnActor<AVC26CourtActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	}
}
