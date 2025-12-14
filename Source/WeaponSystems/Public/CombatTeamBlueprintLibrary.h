// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GenericTeamAgentInterface.h"
#include "CombatTeamUtility.h"
#include "CombatTeamBlueprintLibrary.generated.h"

class APawn;
class AController;

/**
 * Pure and static Blueprint Function Library providing clean, real-world multiplayer workflow functions:
 * 1. GameMode & Player Lifecycle: Spawning players, assigning and switching teams at runtime.
 * 2. Vehicle Possession: Claiming vehicles on enter, reverting to neutral on eject/exit.
 * 3. Combat & Damage Verification: 1-node friendly fire checks and IFF classifications.
 * 4. Ground Units & Props: Easy team assignment for non-radar units (tanks, SAMs, turrets).
 */
UCLASS()
class WEAPONSYSTEMS_API UCombatTeamBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// =========================================================================
	// 1. GAMEMODE & PLAYER TEAMS (Assigning, Switching, Spawning)
	// =========================================================================

	/**
	 * Sets the team ID for a player (Server Authoritative).
	 * Automatically sets the team on the Controller, PlayerState, and whatever Pawn/Vehicle the player is currently driving.
	 * Use in GameMode (OnPostLogin / RestartPlayer) or when a player switches factions in a menu.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Team")
	static void SetPlayerTeam(AController* PlayerController, uint8 NewTeamID);

	/**
	 * Gets a player's team ID from their Controller or PlayerState.
	 * Returns 255 if unassigned or invalid.
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Team")
	static uint8 GetPlayerTeam(const AController* PlayerController);

	// =========================================================================
	// 2. VEHICLE POSSESSION (Boarding & Ejecting)
	// =========================================================================

	/**
	 * Call this inside your vehicle's 'Event Possessed'.
	 * Automatically inherits the pilot's team onto the vehicle's radar and transponder with zero client flicker.
	 * @param Vehicle           The vehicle pawn (plug 'Self')
	 * @param DriverController  The possessing controller (plug 'New Controller')
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Team")
	static void OnVehiclePossessed(APawn* Vehicle, AController* DriverController);

	/**
	 * Call this inside your vehicle's 'Event Unpossessed' (e.g. pilot ejects or exits).
	 * Reverts the abandoned vehicle to Neutral / Derelict (defaults to Team 255).
	 * @param Vehicle           The vehicle pawn (plug 'Self')
	 * @param AbandonedTeamID   Team assigned to empty vehicle (defaults to 255 = Neutral)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Team")
	static void OnVehicleUnpossessed(APawn* Vehicle, uint8 AbandonedTeamID = 255);

	// =========================================================================
	// 3. COMBAT & FRIENDLY FIRE (Damage & Targeting)
	// =========================================================================

	/**
	 * Returns true if ActorA and ActorB belong to the same valid team.
	 * Standard 1-node check for Event AnyDamage to prevent friendly fire.
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Team")
	static bool IsFriendly(const AActor* ActorA, const AActor* ActorB);

	/**
	 * Returns true if ActorA and ActorB belong to opposing valid teams.
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Team")
	static bool IsHostile(const AActor* ActorA, const AActor* ActorB);

	/**
	 * Returns true if either actor has no assigned team (Team 255) or they are neutral.
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Team")
	static bool IsNeutral(const AActor* ActorA, const AActor* ActorB);

	/**
	 * Returns the resolved ETeamAttitude (Friendly, Hostile, Neutral) from Observer toward Target.
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Team")
	static TEnumAsByte<ETeamAttitude::Type> GetTeamAttitude(
		const AActor* Observer,
		const AActor* Target,
		TEnumAsByte<ETeamAttitude::Type> Fallback = ETeamAttitude::Neutral);

	/**
	 * Returns the IFF classification from Observer toward Target (Friendly, Hostile, Neutral, Unknown).
	 * Automatically evaluates target stealth / EMCON mode (returns Unknown if transponder is off).
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Team")
	static ERadarIFFResult GetIFFClassification(const AActor* Observer, const AActor* Target);

	// =========================================================================
	// 4. GENERAL ACTORS & PROPS (Tanks, SAMs, AI Turrets, Spawned Props)
	// =========================================================================

	/**
	 * Sets the team ID on any actor (Ground vehicles, SAM launchers, AI turrets, soldiers, or props).
	 * Authoritative only. Updates transponders, radars, and generic team interfaces.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Team")
	static void SetActorTeam(AActor* TargetActor, uint8 NewTeamID);

	/**
	 * Gets the team ID of any actor, vehicle, soldier, or controller (0-254 = Faction, 255 = Neutral).
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Team")
	static uint8 GetActorTeam(const AActor* TargetActor);

	/**
	 * Toggles transponder active state on an actor equipped with UIFFTransponderComponent.
	 * Set to false for stealth / EMCON silent running (makes unit appear as Unknown on enemy radar).
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Team")
	static void SetTransponderActive(AActor* TargetActor, bool bActive);

	/**
	 * Returns true if the target actor has an active transmitting transponder.
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Team")
	static bool IsTransponderActive(const AActor* TargetActor);

	// =========================================================================
	// 5. Legacy aliases
	// =========================================================================

	UFUNCTION(BlueprintCallable, Category = "Combat|Team", meta = (DeprecatedFunction, DeprecationMessage = "Use OnVehiclePossessed instead"))
	static void SyncPawnTeamFromController(APawn* Pawn, AController* Controller = nullptr, uint8 FallbackTeamID = 255);

	UFUNCTION(BlueprintCallable, Category = "Combat|Team", meta = (DeprecatedFunction, DeprecationMessage = "Use SetActorTeam instead"))
	static void SetActorTeamID(AActor* TargetActor, uint8 NewTeamID);

	UFUNCTION(BlueprintPure, Category = "Combat|Team", meta = (DeprecatedFunction, DeprecationMessage = "Use GetActorTeam instead"))
	static uint8 GetActorTeamID(const AActor* TargetActor);

	UFUNCTION(BlueprintPure, Category = "Combat|Team", meta = (DeprecatedFunction, DeprecationMessage = "Use GetIFFClassification instead"))
	static ERadarIFFResult GetActorIFFResult(const AActor* Observer, const AActor* TargetActor);
};
