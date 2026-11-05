// Placeholder sound effects, synthesised at startup so the prototype needs no
// audio assets.
#pragma once

#include "CoreMinimal.h"

class UAudioComponent;

enum class EVC26Sound : uint8
{
	Pass,
	Set,
	Spike,
	Block,
	Floor,
	Net,
	Whistle,
	Cheer,
	Count
};

class FVC26Sounds
{
public:
	void Init();
	void SetMuted(bool bInMuted) { bMuted = bInMuted; }
	void Play(UObject* WorldContext, EVC26Sound Sound, float Volume = 1.f, float Pitch = 1.f);

	// Procedural waves never finish by themselves, so each one is stopped here.
	void Tick(float DeltaSeconds);

private:
	struct FActive
	{
		TWeakObjectPtr<UAudioComponent> Component;
		float TimeLeft = 0.f;
	};

	TArray<uint8> Clips[static_cast<int32>(EVC26Sound::Count)];
	TArray<FActive> Active;
	bool bMuted = false;
};
