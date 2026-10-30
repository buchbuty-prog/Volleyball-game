// Blueprint-visible mirrors of the vc26 core types.
#pragma once

#include "CoreMinimal.h"
#include "VC26Types.generated.h"

// Order must match vc26::Role / Slot / RuleMode / Legality / System (checked in VC26Types.cpp).
UENUM(BlueprintType)
enum class EVC26Role : uint8 { Setter, OutsideHitter, MiddleBlocker, Opposite, Libero };

UENUM(BlueprintType)
enum class EVC26Slot : uint8 { P1, P2, P3, P4, P5, P6 };

UENUM(BlueprintType)
enum class EVC26RuleMode : uint8 { Simulation, Arcade };

UENUM(BlueprintType)
enum class EVC26Legality : uint8 { Legal, ArcadeOnly, Illegal };

UENUM(BlueprintType)
enum class EVC26System : uint8 { FiveOne UMETA(DisplayName = "5-1"), SixTwo UMETA(DisplayName = "6-2"), FourTwo UMETA(DisplayName = "4-2") };

USTRUCT(BlueprintType)
struct VC26CORE_API FVC26Player
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VC26")
	int32 Id = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VC26")
	EVC26Role Role = EVC26Role::OutsideHitter;
};

// What the pre-match lineup screen produces.
USTRUCT(BlueprintType)
struct VC26CORE_API FVC26Lineup
{
	GENERATED_BODY()

	// Exactly six entries, index 0 = P1 ... index 5 = P6.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VC26")
	TArray<FVC26Player> Starters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VC26")
	bool bHasLibero = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VC26", meta = (EditCondition = "bHasLibero"))
	FVC26Player Libero;

	// If set, the libero replaces the starter in LiberoStartSlot before the first serve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VC26", meta = (EditCondition = "bHasLibero"))
	bool bLiberoStarts = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VC26", meta = (EditCondition = "bLiberoStarts"))
	EVC26Slot LiberoStartSlot = EVC26Slot::P6;

	// Player ids the libero may replace. Empty = the starter in LiberoStartSlot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VC26", meta = (EditCondition = "bHasLibero"))
	TArray<int32> LiberoTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VC26")
	EVC26System System = EVC26System::FiveOne;
};
