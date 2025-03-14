// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GenericTeamAgentInterface.h"
#include "CombatTeamUtility.h"
#include "IFFTransponderComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTransponderActiveChangedSignature, bool, bIsActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquawkCodeChangedSignature, int32, NewSquawkCode);

/**
 * Lightweight, zero-tick transponder component for any actor in the game (infantry, tanks, SAMs, civilian vehicles, props).
 * Provides replicated Team ID, 4-digit Squawk Code, and EMCON stealth toggling without requiring radar components.
 * Implements IGenericTeamAgentInterface natively for seamless integration with AI perception and radar interrogators.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UIFFTransponderComponent : public UActorComponent, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	UIFFTransponderComponent();

	/** Team ID for this actor (0-254 = Factions, 255 = NoTeam / Neutral). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_TeamID, Category = "Combat|IFF")
	uint8 TeamID = 255;

	/** Transponder squawk code (e.g. 1200 VFR, 7700 Emergency, or military Mode 3/A code). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_SquawkCode, Category = "Combat|IFF", meta = (ClampMin = "0", ClampMax = "7777"))
	int32 SquawkCode = 1200;

	/** If true, responds to IFF sweeps with team/squawk. If false (EMCON stealth), returns Unknown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_TransponderActive, Category = "Combat|IFF")
	bool bTransponderActive = true;

	/** Broadcast when this transponder's team ID changes */
	UPROPERTY(BlueprintAssignable, Category = "Combat|IFF|Events")
	FOnCombatTeamChangedSignature OnTeamChanged;

	/** Broadcast when transponder active state is toggled */
	UPROPERTY(BlueprintAssignable, Category = "Combat|IFF|Events")
	FOnTransponderActiveChangedSignature OnTransponderActiveChanged;

	/** Broadcast when squawk code changes */
	UPROPERTY(BlueprintAssignable, Category = "Combat|IFF|Events")
	FOnSquawkCodeChangedSignature OnSquawkCodeChanged;

	/** Sets the actor's team ID (Server/Authoritative). Updates all replicated clients. */
	UFUNCTION(BlueprintCallable, Category = "Combat|IFF")
	void SetTeamID(uint8 NewTeamID);

	/** Returns the assigned team ID */
	UFUNCTION(BlueprintPure, Category = "Combat|IFF")
	uint8 GetTeamID() const { return TeamID; }

	/** Sets whether the transponder is actively transmitting squawk responses (Server/Authoritative). */
	UFUNCTION(BlueprintCallable, Category = "Combat|IFF")
	void SetTransponderActive(bool bActive);

	/** Returns true if the transponder is actively emitting */
	UFUNCTION(BlueprintPure, Category = "Combat|IFF")
	bool IsTransponderActive() const { return bTransponderActive; }

	/** Sets the transponder squawk code (Server/Authoritative). */
	UFUNCTION(BlueprintCallable, Category = "Combat|IFF")
	void SetSquawkCode(int32 NewSquawkCode);

	/** Returns the current squawk code */
	UFUNCTION(BlueprintPure, Category = "Combat|IFF")
	int32 GetSquawkCode() const { return SquawkCode; }

	// ~Begin IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	// ~End IGenericTeamAgentInterface

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_TeamID();

	UFUNCTION()
	void OnRep_SquawkCode();

	UFUNCTION()
	void OnRep_TransponderActive();
};
