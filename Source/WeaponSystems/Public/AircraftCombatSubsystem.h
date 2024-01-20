// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AircraftCombatSubsystem.generated.h"

class UAircraftRadarComponent;
class URadarMissileGuidanceComponent;
class AActor;

/**
 * World Subsystem that maintains a high-performance spatial registry of active
 * combat entities, including airborne radars, active radar missile seekers,
 * and combat aircraft/pawns.
 *
 * Eliminates heavy 150 km - 200 km Chaos physics broadphase sphere overlaps,
 * replacing them with microsecond O(N) mathematical distance and cone evaluations.
 */
UCLASS()
class WEAPONSYSTEMS_API UAircraftCombatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UAircraftCombatSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Static accessor for convenience */
	UFUNCTION(BlueprintPure, Category = "Combat|Subsystem", meta = (WorldContext = "WorldContextObject"))
	static UAircraftCombatSubsystem* Get(const UObject* WorldContextObject);

	/** Registers an airborne or surface radar component */
	UFUNCTION(BlueprintCallable, Category = "Combat|Subsystem")
	void RegisterRadar(UAircraftRadarComponent* Radar);

	/** Unregisters a radar component */
	UFUNCTION(BlueprintCallable, Category = "Combat|Subsystem")
	void UnregisterRadar(UAircraftRadarComponent* Radar);

	/** Returns all registered radars */
	const TArray<TWeakObjectPtr<UAircraftRadarComponent>>& GetRegisteredRadars() const { return RegisteredRadars; }

	/** Retrieves all active, emitting radars within range of a world position */
	void GetRadarsInRange(const FVector& Location, float MaxRange, TArray<UAircraftRadarComponent*>& OutRadars) const;

	/** Registers an active radar-guided missile component */
	UFUNCTION(BlueprintCallable, Category = "Combat|Subsystem")
	void RegisterMissileSeeker(URadarMissileGuidanceComponent* Missile);

	/** Unregisters a missile component */
	UFUNCTION(BlueprintCallable, Category = "Combat|Subsystem")
	void UnregisterMissileSeeker(URadarMissileGuidanceComponent* Missile);

	/** Convenience aliases for missile registration */
	void RegisterRadarMissile(URadarMissileGuidanceComponent* Missile) { RegisterMissileSeeker(Missile); }
	void UnregisterRadarMissile(URadarMissileGuidanceComponent* Missile) { UnregisterMissileSeeker(Missile); }

	/** Returns all registered active missile seekers */
	const TArray<TWeakObjectPtr<URadarMissileGuidanceComponent>>& GetRegisteredMissileSeekers() const { return RegisteredMissiles; }

	/** Retrieves all active radar missile seekers within range of a world position */
	void GetActiveMissileSeekersInRange(const FVector& Location, float MaxRange, TArray<URadarMissileGuidanceComponent*>& OutMissiles) const;

	/** Registers a combat actor/pawn for radar detection */
	UFUNCTION(BlueprintCallable, Category = "Combat|Subsystem")
	void RegisterCombatActor(AActor* Actor);

	/** Unregisters a combat actor/pawn */
	UFUNCTION(BlueprintCallable, Category = "Combat|Subsystem")
	void UnregisterCombatActor(AActor* Actor);

	/** Returns all registered combat actors */
	const TArray<TWeakObjectPtr<AActor>>& GetRegisteredCombatActors() const { return RegisteredCombatActors; }

	/** Retrieves registered combat actors within a sphere volume */
	void GetCombatActorsInVolume(const FVector& Origin, float MaxRange, TArray<AActor*>& OutActors) const;

	/** Retrieves registered combat pawns within a sphere volume */
	void GetCombatPawnsInRange(const FVector& Location, float MaxRange, TArray<APawn*>& OutPawns) const;

private:
	/** Active radar components in the world */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UAircraftRadarComponent>> RegisteredRadars;

	/** Active radar missile guidance components in flight */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<URadarMissileGuidanceComponent>> RegisteredMissiles;

	/** Registered combat pawns / actors */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> RegisteredCombatActors;

	/** Cleans invalid weak pointers from all arrays */
	void PruneStaleRegistrations();
};
