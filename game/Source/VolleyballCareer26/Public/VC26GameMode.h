// Default game mode: makes sure the court is in the level when Play starts.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "VC26GameMode.generated.h"

UCLASS()
class VOLLEYBALLCAREER26_API AVC26GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AVC26GameMode();
	virtual void StartPlay() override;
};
