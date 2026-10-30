// First playable slice: a regulation court built from engine shapes, both
// teams standing in their rotation slots, and keys to play rallies.
//   1 = home wins the rally, 2 = away wins the rally, R = restart the match.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VC26MatchComponent.h"
#include "VC26Types.h"

#include "VC26CourtActor.generated.h"

class ACameraActor;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UVC26TeamComponent;

UCLASS()
class VOLLEYBALLCAREER26_API AVC26CourtActor : public AActor
{
	GENERATED_BODY()

public:
	AVC26CourtActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "VC26")
	EVC26RuleMode Mode = EVC26RuleMode::Simulation;

	UPROPERTY(EditAnywhere, Category = "VC26")
	bool bHomeServesFirst = false;

	// Move the court onto whatever ground is below it when Play starts.
	UPROPERTY(EditAnywhere, Category = "VC26")
	bool bSnapToGround = true;

	UPROPERTY(VisibleAnywhere, Category = "VC26")
	TObjectPtr<UVC26TeamComponent> HomeTeam;

	UPROPERTY(VisibleAnywhere, Category = "VC26")
	TObjectPtr<UVC26TeamComponent> AwayTeam;

	UPROPERTY(VisibleAnywhere, Category = "VC26")
	TObjectPtr<UVC26MatchComponent> Match;

private:
	struct FPlayerVisual
	{
		int32 Id = -1;
		int32 Number = 0;
		FString Name;
		EVC26Role Role = EVC26Role::OutsideHitter;
		bool bHome = true;
		UStaticMeshComponent* Body = nullptr;
		UStaticMeshComponent* Head = nullptr;
		UTextRenderComponent* Label = nullptr;
		FVector Target = FVector::ZeroVector;
	};

	void BuildCourt();
	void BuildTeam(bool bHome);
	void StartMatch();
	void PlayRally(bool bHomeWins);
	void UpdateTargets(bool bSnap);
	FVector SlotPosition(bool bHome, EVC26Slot Slot, bool bServing) const;
	FVector BenchPosition(bool bHome, int32 BenchIndex) const;
	UStaticMeshComponent* AddShape(UStaticMesh* Mesh, const FVector& Center, const FVector& SizeCm, const FLinearColor& Color);

	UFUNCTION()
	void HandleHomeLiberoSwap(EVC26Slot Slot, int32 LiberoId, int32 OtherPlayerId, bool bEntering);

	UFUNCTION()
	void HandleAwayLiberoSwap(EVC26Slot Slot, int32 LiberoId, int32 OtherPlayerId, bool bEntering);

	UFUNCTION()
	void HandleSetWon(EVC26Team Team);

	UFUNCTION()
	void HandleMatchWon(EVC26Team Team);

	UFUNCTION()
	void HandleMatchPoint(EVC26Team Team);

	UFUNCTION()
	void HandleSwitchSides();

	void LogLiberoSwap(bool bHome, EVC26Slot Slot, int32 LiberoId, int32 OtherPlayerId, bool bEntering);
	const FPlayerVisual* FindPlayer(int32 Id) const;

	UPROPERTY()
	TArray<TObjectPtr<UActorComponent>> SpawnedComponents;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	TArray<FPlayerVisual> Players;
	FString LastEvent;
};
