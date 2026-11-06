#include "VC26AthleteAnim.h"

#include "Animation/AnimationPoseData.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "BonePose.h"

namespace
{
	const TCHAR* const BoneNames[] = {
		TEXT("hips"), TEXT("spine"), TEXT("spine1"), TEXT("spine2"), TEXT("neck"), TEXT("head"),
		TEXT("leftshoulder"), TEXT("leftarm"), TEXT("leftforearm"), TEXT("lefthand"),
		TEXT("rightshoulder"), TEXT("rightarm"), TEXT("rightforearm"), TEXT("righthand"),
		TEXT("leftupleg"), TEXT("leftleg"), TEXT("leftfoot"),
		TEXT("rightupleg"), TEXT("rightleg"), TEXT("rightfoot")};

	// Each bone's parent in the list above. Every parent comes before its children.
	const int32 Parents[] = {-1, 0, 1, 2, 3, 4, 3, 6, 7, 8, 3, 10, 11, 12, 0, 14, 15, 0, 17, 18};

	constexpr float MinHipHeight = 32.f;  // the dive ends below the floor without this
	constexpr float MaxLook = 75.f;       // degrees the head turns from the chest
	constexpr float MaxTwist = 70.f;      // degrees the shoulders turn from the hips

	const FVector Up(0.f, 0.f, 1.f);
	const FVector Ahead(0.f, 1.f, 0.f);

	FQuat PartOf(const FQuat& Rotation, float Share)
	{
		return FQuat::Slerp(FQuat::Identity, Rotation, Share);
	}

	FQuat Between(const FVector& From, const FVector& To)
	{
		return FQuat::FindBetweenNormals(From.GetSafeNormal(), To.GetSafeNormal());
	}
}

void FVC26AthleteProxy::PreUpdate(UAnimInstance* Instance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
	Drive = CastChecked<UVC26AthleteAnim>(Instance)->Drive;
}

void FVC26AthleteProxy::FindBones(const FBoneContainer& Container)
{
	bBonesFound = true;
	BonesSerial = Container.GetSerialNumber();
	for (int32 i = 0; i < BoneCount; ++i)
	{
		const int32 MeshIndex = Container.GetPoseBoneIndexForBoneName(FName(BoneNames[i]));
		if (MeshIndex == INDEX_NONE)
		{
			bBonesFound = false;
			return;
		}
		Bones[i] = Container.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (Bones[i] == INDEX_NONE)
		{
			bBonesFound = false;
			return;
		}
		const FTransform& Local = Container.GetRefPoseTransform(Bones[i]);
		Rest[i] = Parents[i] < 0 ? Local : Local * Rest[Parents[i]];
	}
	LeftSide = Rest[ArmL].GetLocation().X > 0.f ? 1.f : -1.f;
}

void FVC26AthleteProxy::Refresh(const FCompactPose& Pose)
{
	for (int32 i = 0; i < BoneCount; ++i)
	{
		Space[i] = Parents[i] < 0 ? Pose[Bones[i]] : Pose[Bones[i]] * Space[Parents[i]];
	}
}

void FVC26AthleteProxy::SetBoneRotation(FCompactPose& Pose, EBone Bone, const FQuat& Rotation)
{
	const FQuat Parent = Parents[Bone] < 0 ? FQuat::Identity : Space[Parents[Bone]].GetRotation();
	Pose[Bones[Bone]].SetRotation((Parent.Inverse() * Rotation).GetNormalized());
}

void FVC26AthleteProxy::RotateBone(FCompactPose& Pose, EBone Bone, const FQuat& Delta)
{
	SetBoneRotation(Pose, Bone, Delta * Space[Bone].GetRotation());
	Refresh(Pose);
}

void FVC26AthleteProxy::SolveLimb(FCompactPose& Pose, EBone Upper, EBone Lower, EBone End, const FVector& Target, const FVector& Pole)
{
	const FVector Start = Space[Upper].GetLocation();
	const float Length1 = FVector::Dist(Start, Space[Lower].GetLocation());
	const float Length2 = FVector::Dist(Space[Lower].GetLocation(), Space[End].GetLocation());

	const FVector Along = (Target - Start).GetSafeNormal();
	const float Reach = FMath::Clamp(FVector::Dist(Start, Target), FMath::Abs(Length1 - Length2) + 1.f, (Length1 + Length2) * 0.999f);

	// The middle joint sits off the straight line, on the pole's side.
	const float Offset = (Length1 * Length1 - Length2 * Length2 + Reach * Reach) / (2.f * Reach);
	const float Height = FMath::Sqrt(FMath::Max(Length1 * Length1 - Offset * Offset, 0.f));
	const FVector Bend = (Pole - Along * FVector::DotProduct(Pole, Along)).GetSafeNormal();
	const FVector Joint = Start + Along * Offset + Bend * Height;

	RotateBone(Pose, Upper, Between(Space[Lower].GetLocation() - Start, Joint - Start));
	RotateBone(Pose, Lower, Between(Space[End].GetLocation() - Space[Lower].GetLocation(), Start + Along * Reach - Space[Lower].GetLocation()));
}

void FVC26AthleteProxy::SolveArm(FCompactPose& Pose, bool bLeft)
{
	const float Out = bLeft ? LeftSide : -LeftSide;
	const EBone Arm = bLeft ? ArmL : ArmR;
	const EBone Forearm = bLeft ? ForearmL : ForearmR;
	const EBone Hand = bLeft ? HandL : HandR;

	// Places are measured from the chest and turn with it.
	const FVector Chest = Space[Spine2].GetLocation();
	const FQuat Frame = Space[Spine2].GetRotation() * Rest[Spine2].GetRotation().Inverse();
	auto FromChest = [&](float X, float Y, float Z) { return Chest + Frame.RotateVector(FVector(X * Out, Y, Z)); };

	FVector Target = FVector::ZeroVector;
	FVector Pole = FVector::ZeroVector;
	float Total = 0.f;
	auto Add = [&](float Weight, const FVector& Place, const FVector& Bend)
	{
		Target += Place * Weight;
		Pole += Bend * Weight;
		Total += Weight;
	};

	Add(Drive.Ready, FromChest(20.f, 44.f, -8.f), FVector(Out, -0.4f, -1.f));

	// The platform points at the ball, low and in front. Going past where the
	// arms reach is what straightens the elbows.
	const FVector Shoulders = (Space[ArmL].GetLocation() + Space[ArmR].GetLocation()) / 2.f;
	const FVector ToReach = (Drive.Reach - Shoulders).GetSafeNormal();
	const FVector Low = FVector(ToReach.X, FMath::Max(ToReach.Y, 0.55f), FMath::Clamp(ToReach.Z, -0.8f, -0.38f)).GetSafeNormal();
	Add(Drive.Platform, Shoulders + Low * 75.f + FVector(1.5f * Out, 0.f, 0.f), FVector(Out, 0.f, -0.5f));

	Add(Drive.Overhead, FromChest(9.f, 30.f, 40.f), FVector(Out, -0.1f, -0.4f));
	Add(Drive.Block, FromChest(19.f, 26.f, 95.f), FVector(Out, -0.3f, 0.f));

	if (bLeft)
	{
		Add(Drive.Toss, FromChest(6.f, 30.f, 70.f), FVector(Out, 0.f, -1.f));
		Add(Drive.Hit, FromChest(14.f, 30.f, -8.f), FVector(Out, 0.f, -1.f));
	}
	else
	{
		Add(Drive.Toss, FromChest(30.f, -16.f, 32.f), FVector(Out, -0.5f, -0.2f));
		Add(Drive.Hit, Drive.Reach, FVector(Out, -0.6f, -0.3f));
		Add(Drive.Cheer, FromChest(16.f, 10.f, 90.f), FVector(Out, 0.f, -0.2f));
	}

	if (Total <= 0.01f)
	{
		return;
	}
	const float Strength = FMath::Min(Total, 1.f);
	const FVector Current = Space[Hand].GetLocation();
	const FVector CurrentBend = Space[Forearm].GetLocation() - (Space[Arm].GetLocation() + Current) / 2.f;
	SolveLimb(Pose, Arm, Forearm, Hand,
		FMath::Lerp(Current, Target / Total, Strength),
		FMath::Lerp(CurrentBend.GetSafeNormal(), (Pole / Total).GetSafeNormal(), Strength));

	// Straight wrists, ready to take the ball.
	const FQuat Straight = Space[Forearm].GetRotation() * Rest[Forearm].GetRotation().Inverse() * Rest[Hand].GetRotation();
	SetBoneRotation(Pose, Hand, FQuat::Slerp(Space[Hand].GetRotation(), Straight, Strength));
	Refresh(Pose);
}

void FVC26AthleteProxy::Solve(FCompactPose& Pose)
{
	Refresh(Pose);
	const FTransform PlantedL = Space[FootL];
	const FTransform PlantedR = Space[FootR];

	// Stance. The hips sit back as they drop, like a real athlete's.
	FTransform& HipsLocal = Pose[Bones[Hips]];
	HipsLocal.AddToTranslation(FVector(0.f, -Drive.Crouch * 0.3f, -Drive.Crouch));
	Refresh(Pose);
	if (!FMath::IsNearlyZero(Drive.Lean))
	{
		const float Angle = FMath::DegreesToRadians(Drive.Lean);
		const FQuat Tip = Between(Up, Up * FMath::Cos(Angle) + Ahead * FMath::Sin(Angle));
		RotateBone(Pose, Spine, PartOf(Tip, 0.6f));
		RotateBone(Pose, Spine1, PartOf(Tip, 0.4f));
	}

	// The legs run where the hips point, and the shoulders stay on the ball.
	const FVector Torso = FVector(Drive.Torso.X, Drive.Torso.Y, 0.f);
	if (!Torso.IsNearlyZero())
	{
		const FQuat Full = Between(Ahead, Torso);
		const float Angle = FMath::RadiansToDegrees(Full.GetAngle());
		const FQuat Twist = PartOf(Full, Angle > MaxTwist ? MaxTwist / Angle : 1.f);
		for (const EBone Bone : {Spine, Spine1, Spine2})
		{
			RotateBone(Pose, Bone, PartOf(Twist, 1.f / 3.f));
		}
	}

	// Feet stay where they were unless the stance or a jump moves them.
	if (Drive.Crouch > 0.5f || Drive.Spread > 0.5f || Drive.Tuck > 0.01f)
	{
		const FVector Fold = FVector(0.f, -34.f, 46.f) * Drive.Tuck;
		const FVector Wide(Drive.Spread * LeftSide, 0.f, 0.f);
		SolveLimb(Pose, ThighL, ShinL, FootL, PlantedL.GetLocation() + Wide + Fold, Ahead + Wide.GetSafeNormal() * 0.3f);
		SolveLimb(Pose, ThighR, ShinR, FootR, PlantedR.GetLocation() - Wide + Fold * 0.8f, Ahead - Wide.GetSafeNormal() * 0.3f);
		SetBoneRotation(Pose, FootL, PlantedL.GetRotation());
		SetBoneRotation(Pose, FootR, PlantedR.GetRotation());
		Refresh(Pose);
	}

	SolveArm(Pose, true);
	SolveArm(Pose, false);

	// Eyes on the ball.
	if (Drive.LookWeight > 0.01f)
	{
		const FVector Face = Space[Head].GetRotation().RotateVector(Rest[Head].GetRotation().UnrotateVector(Ahead));
		const FQuat Full = Between(Face, Drive.Look - Space[Head].GetLocation());
		const float Angle = FMath::RadiansToDegrees(Full.GetAngle());
		const FQuat Turn = PartOf(Full, Drive.LookWeight * (Angle > MaxLook ? MaxLook / Angle : 1.f));
		RotateBone(Pose, Neck, PartOf(Turn, 0.45f));
		RotateBone(Pose, Head, PartOf(Turn, 0.55f));
	}
}

bool FVC26AthleteProxy::Evaluate(FPoseContext& Output)
{
	if (!Drive.Clip)
	{
		Output.ResetToRefPose();
		return true;
	}

	FAnimationPoseData Data(Output);
	Drive.Clip->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Drive.Time), false, FDeltaTimeRecord(), Drive.bLoop));

	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Container = Pose.GetBoneContainer();
	if (!bBonesFound || BonesSerial != Container.GetSerialNumber())
	{
		FindBones(Container);
	}
	if (!bBonesFound)
	{
		return true;
	}

	// The sim alone decides where a player is, so the clip's own travel goes.
	FTransform& HipsLocal = Pose[Bones[Hips]];
	FVector HipsAt = HipsLocal.GetTranslation();
	HipsAt.X *= Drive.KeepTravel;
	HipsAt.Y *= Drive.KeepTravel;
	HipsAt.Z = FMath::Max(HipsAt.Z, MinHipHeight);
	HipsLocal.SetTranslation(HipsAt);

	// Crossfades start from whatever was on screen, so a change of mind mid-fade never pops.
	const int32 BoneTotal = Pose.GetNumBones();
	if (Drive.Transition != SeenTransition)
	{
		SeenTransition = Drive.Transition;
		FromPose = LastPose;
	}
	if (Drive.Fade < 1.f && FromPose.Num() == BoneTotal)
	{
		const float Alpha = FMath::SmoothStep(0.f, 1.f, Drive.Fade);
		for (FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
		{
			FTransform Blended = FromPose[Index.GetInt()];
			Blended.BlendWith(Pose[Index], Alpha);
			Pose[Index] = Blended;
		}
	}
	LastPose.SetNumUninitialized(BoneTotal);
	for (FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
	{
		LastPose[Index.GetInt()] = Pose[Index];
	}

	Solve(Pose);
	return true;
}
