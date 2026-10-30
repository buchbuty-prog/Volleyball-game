// One team's rotation, libero automation and Role-Fit (GDD 1.7).
// Add one to each team actor; call RallyEnd once per dead ball.
#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "VC26Types.h"
#include "vc26/RotationManager.h"

#include <optional>

#include "VC26TeamComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVC26SideOutSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FVC26LiberoSwapSignature, EVC26Slot, Slot, int32, LiberoId, int32, OtherPlayerId, bool, bEntering);

UCLASS(ClassGroup = (VC26), meta = (BlueprintSpawnableComponent))
class VC26CORE_API UVC26TeamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Checks a lineup for the lineup screen. OutSlotColors has six entries (P1..P6).
	UFUNCTION(BlueprintPure, Category = "VC26|Lineup")
	static EVC26Legality ValidateLineup(const FVC26Lineup& Lineup, bool bServingFirst, TArray<EVC26Legality>& OutSlotColors, TArray<FString>& OutIssues);

	// Returns false (and keeps the previous state) if the lineup is not allowed in Mode.
	UFUNCTION(BlueprintCallable, Category = "VC26|Rotation")
	bool StartSet(const FVC26Lineup& Lineup, EVC26RuleMode Mode, bool bServingFirst);

	UFUNCTION(BlueprintCallable, Category = "VC26|Rotation")
	void RallyEnd(bool bWonByUs);

	UFUNCTION(BlueprintPure, Category = "VC26|Rotation")
	bool HasStarted() const { return Rotation.has_value(); }

	UFUNCTION(BlueprintPure, Category = "VC26|Rotation")
	bool IsServing() const;

	// -1 if the set has not started.
	UFUNCTION(BlueprintPure, Category = "VC26|Rotation")
	int32 GetPlayerAt(EVC26Slot Slot) const;

	// False if the player is not on court.
	UFUNCTION(BlueprintPure, Category = "VC26|Rotation")
	bool GetSlotOf(int32 PlayerId, EVC26Slot& OutSlot) const;

	// Role-Fit multiplier (0.6-1.0) for a player on court; 1.0 otherwise.
	UFUNCTION(BlueprintPure, Category = "VC26|RoleFit")
	float GetRoleFit(int32 PlayerId) const;

	UPROPERTY(BlueprintAssignable, Category = "VC26|Rotation")
	FVC26SideOutSignature OnSideOut;

	UPROPERTY(BlueprintAssignable, Category = "VC26|Rotation")
	FVC26LiberoSwapSignature OnLiberoSwap;

private:
	std::optional<vc26::RotationManager> Rotation;
};
