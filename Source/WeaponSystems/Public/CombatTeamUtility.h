// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "CombatTeamUtility.generated.h"

enum class ERadarIFFResult : uint8;

/**
 * Fallback attitude policy when an actor does not implement IGenericTeamAgentInterface.
 * Configured per-component to allow different behavior for different systems.
 */
UENUM(BlueprintType)
enum class EIFFUnknownAttitude : uint8
{
	Hostile  UMETA(DisplayName = "Treat as Hostile"),
	Neutral  UMETA(DisplayName = "Treat as Neutral"),
	Friendly UMETA(DisplayName = "Treat as Friendly")
};

/** Broadcast when an actor or component's combat team ID changes */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatTeamChangedSignature, uint8, NewTeamID);


/**
 * Central static utility for resolving IFF (Identification Friend or Foe) using
 * Unreal Engine's IGenericTeamAgentInterface. All combat components delegate
 * team attitude queries through this class to avoid duplicating resolution logic.
 *
 * Resolution Order:
 *   1. Check the actor directly for IGenericTeamAgentInterface
 *   2. If the actor is a Pawn, check its Controller (Server & Autonomous Proxy)
 *   3. If Controller lacks it, check PlayerState (replicated to all clients / Simulated Proxies, Lyra-compatible)
 *   4. If none implement the interface, return the caller-specified fallback attitude
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FCombatTeamUtility
{
	GENERATED_BODY()

	/**
	 * Resolves the team attitude from Observer toward Target.
	 * Falls back to the specified attitude if either actor lacks IGenericTeamAgentInterface.
	 *
	 * @param Observer  The actor performing the query (e.g. the radar's owner pawn)
	 * @param Target    The actor being evaluated (e.g. a detected contact)
	 * @param Fallback  Attitude to return if team resolution fails
	 * @return          Friendly, Hostile, or Neutral
	 */
	static ETeamAttitude::Type GetAttitude(
		const AActor* Observer,
		const AActor* Target,
		ETeamAttitude::Type Fallback = ETeamAttitude::Neutral);

	/** Returns true if Target is friendly to Observer */
	static bool IsFriendly(const AActor* Observer, const AActor* Target);

	/** Returns true if Target is hostile to Observer */
	static bool IsHostile(const AActor* Observer, const AActor* Target);

	/**
	 * Resolves IGenericTeamAgentInterface from an actor.
	 * Checks the actor directly first, then its controller if the actor is a Pawn.
	 */
	static const IGenericTeamAgentInterface* ResolveTeamAgent(const AActor* Actor);

	/** Maps ETeamAttitude to ERadarIFFResult */
	static ERadarIFFResult AttitudeToIFF(ETeamAttitude::Type Attitude);

	/** Maps EIFFUnknownAttitude to ETeamAttitude for fallback resolution */
	static ETeamAttitude::Type UnknownAttitudeToTeamAttitude(EIFFUnknownAttitude InAttitude);
};
