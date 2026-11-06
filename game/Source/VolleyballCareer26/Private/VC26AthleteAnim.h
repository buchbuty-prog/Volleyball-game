// How a rigged athlete moves. Mixamo has no volleyball animations, so one clip
// plays underneath and the volleyball is added on top: crossfades between clips,
// a stance with planted feet, arms that reach for the ball, and a head that
// follows it. The arena fills in FVC26AthleteDrive every frame.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "VC26AthleteAnim.generated.h"

class UAnimSequence;

// Positions and directions are in the athlete's own space, unscaled: +Y is
// forward and +Z up, with the feet at zero. Weights run 0 to 1.
struct FVC26AthleteDrive
{
	const UAnimSequence* Clip = nullptr;
	float Time = 0.f;
	bool bLoop = true;
	uint32 Transition = 0;  // a new value fades in from the pose on screen
	float Fade = 1.f;       // 0 is that old pose, 1 the clip
	float KeepTravel = 0.f; // the share of the clip's own hip travel to keep

	float Crouch = 0.f;  // hips drop this far, in cm
	float Lean = 0.f;    // torso tips forward, in degrees
	float Spread = 0.f;  // each foot moves out this far, in cm
	float Tuck = 0.f;    // legs fold up behind, in the air
	float Turn = 0.f;    // shoulders coil, hitting arm back, in degrees

	float Ready = 0.f;     // hands out in front, waiting
	float Platform = 0.f;  // forearms together for a pass
	float Overhead = 0.f;  // hands above the forehead for a set
	float Block = 0.f;     // arms straight up
	float Toss = 0.f;      // serve: left arm up, right arm back
	float Hit = 0.f;       // right hand to Reach
	float Cheer = 0.f;     // a fist in the air
	float Back = 0.f;      // both arms behind the hips, loading a jump
	float Draw = 0.f;      // bow and arrow: left arm up, right elbow high and back
	float Follow = 0.f;    // the hitting arm finishes across the body
	float Hold = 0.f;      // the ball held out, ready to serve
	float Open = 0.f;      // fingers spread

	FVector Reach = FVector(0.f, 60.f, 150.f);  // where the ball is met
	FVector Torso = FVector(0.f, 1.f, 0.f);     // the way the shoulders face
	FVector Look = FVector(0.f, 100.f, 170.f);  // what the eyes follow
	float LookWeight = 0.f;

	// The player's own proportions, from vc26::BodyShape.
	float LegScale = 1.f;
	float ArmScale = 1.f;
	float ShoulderWidth = 1.f;
	float HeadScale = 1.f;
};

struct FVC26AthleteProxy : public FAnimInstanceProxy
{
	FVC26AthleteProxy() = default;
	explicit FVC26AthleteProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	enum EBone
	{
		Hips, Spine, Spine1, Spine2, Neck, Head,
		ShoulderL, ArmL, ForearmL, HandL,
		ShoulderR, ArmR, ForearmR, HandR,
		ThighL, ShinL, FootL,
		ThighR, ShinR, FootR,
		BoneCount
	};

	void FindBones(const FBoneContainer& Container);
	void ApplyBuild(FCompactPose& Pose);
	void Solve(FCompactPose& Pose);
	void Refresh(const FCompactPose& Pose);
	void RotateBone(FCompactPose& Pose, EBone Bone, const FQuat& Delta);
	void SetBoneRotation(FCompactPose& Pose, EBone Bone, const FQuat& Rotation);
	void SolveLimb(FCompactPose& Pose, EBone Upper, EBone Lower, EBone End, const FVector& Target, const FVector& Pole);
	void SolveArm(FCompactPose& Pose, bool bLeft);

	FVC26AthleteDrive Drive;

	FCompactPoseBoneIndex Bones[BoneCount] = {
		FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0),
		FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0),
		FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0),
		FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0), FCompactPoseBoneIndex(0)};
	TArray<FCompactPoseBoneIndex> Fingers;
	bool bBonesFound = false;
	uint16 BonesSerial = 0;
	FTransform Rest[BoneCount];   // the T-pose, in the athlete's space
	FTransform Space[BoneCount];  // the current pose, in the athlete's space
	float LeftSide = 1.f;         // which way +X is for the left arm

	TArray<FTransform> LastPose;  // what was on screen, for crossfades
	TArray<FTransform> FromPose;  // what the running crossfade started from
	uint32 SeenTransition = 0;
};

UCLASS(Transient, NotBlueprintable)
class UVC26AthleteAnim : public UAnimInstance
{
	GENERATED_BODY()

public:
	FVC26AthleteDrive Drive;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FVC26AthleteProxy(this); }
};
