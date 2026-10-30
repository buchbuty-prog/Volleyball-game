// Set and match scoring (indoor or beach). Put one on the GameState and call
// AwardPoint once per rally; it fires the events the camera and commentary use.
#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "vc26/MatchScore.h"

#include "VC26MatchComponent.generated.h"

UENUM(BlueprintType)
enum class EVC26Team : uint8 { Home, Away };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVC26TeamSignature, EVC26Team, Team);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVC26MatchEventSignature);

UCLASS(ClassGroup = (VC26), meta = (BlueprintSpawnableComponent))
class VC26CORE_API UVC26MatchComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "VC26|Score")
	void StartMatch(bool bBeach);

	UFUNCTION(BlueprintCallable, Category = "VC26|Score")
	void AwardPoint(EVC26Team Scorer);

	UFUNCTION(BlueprintPure, Category = "VC26|Score")
	int32 GetPoints(EVC26Team Team) const { return Score.points(ToCore(Team)); }

	UFUNCTION(BlueprintPure, Category = "VC26|Score")
	int32 GetSets(EVC26Team Team) const { return Score.sets(ToCore(Team)); }

	UFUNCTION(BlueprintPure, Category = "VC26|Score")
	int32 GetSetNumber() const { return Score.setNumber(); }

	UFUNCTION(BlueprintPure, Category = "VC26|Score")
	bool IsMatchPoint() const { return Score.isMatchPoint(); }

	UFUNCTION(BlueprintPure, Category = "VC26|Score")
	bool IsFinished() const { return Score.finished(); }

	UPROPERTY(BlueprintAssignable, Category = "VC26|Score")
	FVC26TeamSignature OnSetWon;

	UPROPERTY(BlueprintAssignable, Category = "VC26|Score")
	FVC26TeamSignature OnMatchWon;

	UPROPERTY(BlueprintAssignable, Category = "VC26|Score")
	FVC26TeamSignature OnSetPoint;

	UPROPERTY(BlueprintAssignable, Category = "VC26|Score")
	FVC26TeamSignature OnMatchPoint;

	UPROPERTY(BlueprintAssignable, Category = "VC26|Score")
	FVC26MatchEventSignature OnSwitchSides;

private:
	static vc26::Team ToCore(EVC26Team T) { return T == EVC26Team::Home ? vc26::Team::Home : vc26::Team::Away; }
	static EVC26Team FromCore(vc26::Team T) { return T == vc26::Team::Home ? EVC26Team::Home : EVC26Team::Away; }

	vc26::MatchScore Score;
};
