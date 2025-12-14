// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AircraftDataLinkSubsystem.generated.h"

class UAircraftRadarComponent;

/**
 * Server-authoritative scheduler, routing mesh, and radio topology manager for tactical data-link networks.
 * Simulates line-of-sight propagation, multi-hop relay routing, and periodic track donation between participants.
 */
UCLASS()
class WEAPONSYSTEMS_API UAircraftDataLinkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	//~ Begin UTickableWorldSubsystem Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End UTickableWorldSubsystem Interface

	/**
	 * Registers an airborne radar component with the data-link subsystem.
	 * Must have server authority and a valid owning actor.
	 * @param Radar The radar component joining the data-link radio network.
	 */
	void RegisterRadar(UAircraftRadarComponent* Radar);

	/**
	 * Unregisters an airborne radar component from the data-link network.
	 * Flushes active queues and participant IDs.
	 * @param Radar The radar component leaving the network.
	 */
	void UnregisterRadar(UAircraftRadarComponent* Radar);

	/**
	 * Returns the number of currently active data-link participants.
	 */
	UFUNCTION(BlueprintPure, Category = "Aircraft Combat|Data Link")
	int32 GetParticipantCount() const { return Participants.Num(); }

	/**
	 * Sets the interval between periodic data-link track broadcast windows.
	 * @param InInterval Time in seconds (clamped to min 0.05s).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aircraft Combat|Data Link")
	void SetTransmissionInterval(float InInterval);

	/**
	 * Returns the interval in seconds between data-link transmission windows.
	 */
	UFUNCTION(BlueprintPure, Category = "Aircraft Combat|Data Link")
	float GetTransmissionInterval() const { return TransmissionInterval; }

	/**
	 * Sets the maximum number of track reports broadcast per participant during each transmission window.
	 * @param InReports Maximum track reports (clamped to min 1).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aircraft Combat|Data Link")
	void SetReportsPerWindow(int32 InReports);

	/**
	 * Returns the maximum number of track reports broadcast per participant per transmission window.
	 */
	UFUNCTION(BlueprintPure, Category = "Aircraft Combat|Data Link")
	int32 GetReportsPerWindow() const { return ReportsPerWindow; }

	/**
	 * Sets the maximum number of intermediate relay hops permitted for message forwarding.
	 * @param InHops Maximum hops allowed (0 = direct line-of-sight only, no relays).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aircraft Combat|Data Link")
	void SetMaxRelayHops(int32 InHops);

	/**
	 * Returns the maximum number of relay hops permitted across the network.
	 */
	UFUNCTION(BlueprintPure, Category = "Aircraft Combat|Data Link")
	int32 GetMaxRelayHops() const { return MaxRelayHops; }

protected:
	/** Time between scheduled transmission windows in seconds. Initialized from AircraftCombatSettings. */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Combat|Data Link")
	float TransmissionInterval = 0.5f;

	/** Maximum track reports transmitted per participant per window. Initialized from AircraftCombatSettings. */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Combat|Data Link")
	int32 ReportsPerWindow = 16;

	/** Maximum relay hops allowed across the ad-hoc mesh network. Initialized from AircraftCombatSettings. */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Combat|Data Link")
	int32 MaxRelayHops = 4;

private:
	/** Active participant radars registered with the subsystem. */
	TArray<TWeakObjectPtr<UAircraftRadarComponent>> Participants;

	/** Round-robin report index cursor per participant ID. */
	TMap<int32, int32> NextReportIndex;

	/** Monotonically increasing participant ID generator. */
	int32 NextParticipantID = 1;

	/** Accumulator for transmission window cadence. */
	float TransmissionAccumulator = 0.0f;

	/** Evaluates whether two radar nodes have mutual RF line-of-sight and are within maximum operational range. */
	bool HasPhysicalPath(const UAircraftRadarComponent* First, const UAircraftRadarComponent* Second,
		const FVector& From, const FVector& To) const;

	/** Executes a scheduled broadcast window, computing network connectivity and routing track reports. */
	void TransmitWindow(float WorldTime);
};
