#include "VC26Sounds.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	constexpr int32 SampleRate = 44100;

	// Calls Sample(t) for every sample and stores the result as 16-bit PCM.
	template <typename FSample>
	TArray<uint8> Render(float Seconds, FSample Sample)
	{
		const int32 Count = FMath::RoundToInt(Seconds * SampleRate);
		TArray<uint8> Pcm;
		Pcm.SetNumUninitialized(Count * 2);
		int16* Out = reinterpret_cast<int16*>(Pcm.GetData());
		for (int32 i = 0; i < Count; ++i)
		{
			const float Fade = FMath::Min(1.f, (Count - i) / 200.f);  // avoids a click at the end
			Out[i] = static_cast<int16>(FMath::Clamp(Sample(static_cast<float>(i) / SampleRate) * Fade, -1.f, 1.f) * 32000.f);
		}
		return Pcm;
	}

	// A pitched thump: the frequency falls from Start to End as it decays.
	auto Thump(float Start, float End, float Decay)
	{
		return [Start, End, Decay, Phase = 0.f](float T) mutable
		{
			Phase += 2.f * PI * FMath::Lerp(End, Start, FMath::Exp(-T * 30.f)) / SampleRate;
			return FMath::Sin(Phase) * FMath::Exp(-T * Decay);
		};
	}

	// White noise through a one-pole low-pass. Smaller Smooth sounds darker.
	auto Noise(int32 Seed, float Smooth)
	{
		return [Random = FRandomStream(Seed), Smooth, Last = 0.f]() mutable
		{
			Last += (Random.FRandRange(-1.f, 1.f) - Last) * Smooth;
			return Last;
		};
	}
}

void FVC26Sounds::Init()
{
	auto Clip = [this](EVC26Sound Sound) -> TArray<uint8>& { return Clips[static_cast<int32>(Sound)]; };

	{
		auto Body = Thump(190.f, 95.f, 28.f);
		auto Slap = Noise(1, 0.5f);
		Clip(EVC26Sound::Pass) = Render(0.22f, [&](float T) { return Body(T) * 0.8f + Slap() * FMath::Exp(-T * 180.f) * 0.5f; });
	}
	{
		auto Body = Thump(430.f, 300.f, 55.f);
		Clip(EVC26Sound::Set) = Render(0.12f, [&](float T) { return Body(T) * 0.6f; });
	}
	{
		auto Body = Thump(160.f, 80.f, 22.f);
		auto Crack = Noise(2, 0.9f);
		Clip(EVC26Sound::Spike) = Render(0.3f, [&](float T) { return Body(T) * 0.8f + Crack() * FMath::Exp(-T * 70.f); });
	}
	{
		auto Body = Thump(260.f, 130.f, 30.f);
		auto Slap = Noise(3, 0.7f);
		Clip(EVC26Sound::Block) = Render(0.25f, [&](float T) { return Body(T) * 0.8f + Slap() * FMath::Exp(-T * 60.f) * 0.8f; });
	}
	{
		auto Body = Thump(95.f, 60.f, 16.f);
		Clip(EVC26Sound::Floor) = Render(0.35f, [&](float T) { return Body(T); });
	}
	{
		auto Rustle = Noise(4, 0.15f);
		Clip(EVC26Sound::Net) = Render(0.4f, [&](float T) { return Rustle() * FMath::Exp(-T * 9.f) * 2.f; });
	}
	Clip(EVC26Sound::Whistle) = Render(0.45f, [](float T)
	{
		const float Trill = 0.75f + 0.25f * FMath::Sin(2.f * PI * 32.f * T);
		const float Envelope = FMath::Min(T / 0.02f, 1.f) * FMath::Min((0.45f - T) / 0.05f, 1.f);
		return (FMath::Sin(2.f * PI * 2900.f * T) + FMath::Sin(2.f * PI * 3050.f * T)) * 0.3f * Trill * Envelope;
	});
	{
		auto Crowd = Noise(5, 0.12f);
		Clip(EVC26Sound::Cheer) = Render(1.8f, [&](float T)
		{
			const float Swell = FMath::Min(T / 0.25f, 1.f) * FMath::Exp(-FMath::Max(T - 0.5f, 0.f) * 2.5f);
			const float Voices = 0.8f + 0.2f * FMath::Sin(2.f * PI * 7.f * T);
			return Crowd() * Swell * Voices * 2.5f;
		});
	}
}

void FVC26Sounds::Play(UObject* WorldContext, EVC26Sound Sound, float Volume, float Pitch)
{
	const TArray<uint8>& Pcm = Clips[static_cast<int32>(Sound)];

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = 1;
	Wave->SoundGroup = SOUNDGROUP_Effects;
	Wave->bLooping = false;
	Wave->QueueAudio(Pcm.GetData(), Pcm.Num());

	if (UAudioComponent* Component = UGameplayStatics::SpawnSound2D(WorldContext, Wave, Volume, Pitch))
	{
		const float Seconds = static_cast<float>(Pcm.Num()) / (2 * SampleRate * Pitch);
		Active.Add({Component, Seconds + 0.1f});
	}
}

void FVC26Sounds::Tick(float DeltaSeconds)
{
	for (int32 i = Active.Num() - 1; i >= 0; --i)
	{
		Active[i].TimeLeft -= DeltaSeconds;
		if (Active[i].TimeLeft <= 0.f)
		{
			if (UAudioComponent* Component = Active[i].Component.Get())
			{
				Component->Stop();
			}
			Active.RemoveAtSwap(i);
		}
	}
}
