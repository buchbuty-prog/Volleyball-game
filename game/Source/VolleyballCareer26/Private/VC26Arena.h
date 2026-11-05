// Draws a vc26::RallySim and feeds it input. All the gameplay lives in core/;
// the court, players and ball here are placeholder shapes built at runtime, so
// the prototype needs no content assets.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "VC26Sounds.h"
#include "vc26/RallySim.h"
#include "vc26/TouchControls.h"
#include "VC26Arena.generated.h"

class UCameraComponent;
class UStaticMesh;
class UStaticMeshComponent;

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

private:
	enum class EPose : uint8 { Idle, Pass, Set, AttackWindUp, AttackSwing, Block, ServeToss };

	// Per-player animation state. The shapes themselves live in Limbs.
	struct FFigure
	{
		FVector LastFeet = FVector::ZeroVector;
		FVector Facing = FVector::ForwardVector;
		float StridePhase = 0.f;
		float RunAmount = 0.f;
		EPose Swing = EPose::Idle;  // held briefly after touching the ball
		float SwingLeft = 0.f;
	};

	UStaticMeshComponent* AddShape(UStaticMesh* Mesh, const FVector& CentreCm, const FVector& SizeCm, const FLinearColor& Color, bool bCastShadow = true);
	void AddLine(float X, float Y, float LengthX, float LengthY);
	void BuildCourt();
	void BuildLights();
	void NewMatch();
	void UpdateTouch();
	vc26::SimInput ReadInput() const;
	void HandleEvents();
	void SyncShapes(float DeltaSeconds);
	EPose PoseFor(int32 Player) const;
	void AnimateFigure(int32 Player, float DeltaSeconds);
	void PlaceLimb(int32 Limb, const FVector& From, const FVector& To, float Thickness);
	void SetDifficulty(int32 Level);
	void UpdateCamera(float DeltaSeconds);
	void SetBanner(const FString& Text);

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

	// Six per player: torso, head, two arms, two legs.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Limbs;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BallShape;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> LandingMarker;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ControlledMarker;

	TUniquePtr<vc26::RallySim> Sim;
	float Accumulator = 0.f;
	FString Banner;
	float BannerTimeLeft = 0.f;
	bool bCameraReady = false;
	FVector CameraPosition = FVector::ZeroVector;
	float ShakeLeft = 0.f;
	float SlowMoLeft = 0.f;
	int32 Difficulty = 1;  // 0 easy, 1 normal, 2 hard
	bool bUseTouch = false;
	vc26::TouchControls Touch;
	FFigure Figures[12];
	FVC26Sounds Sounds;
};
