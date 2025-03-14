// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "AircraftCombatCommonTypes.generated.h"

/**
 * Operational combat environment / domain of a contact or weapon platform
 */
UENUM(BlueprintType)
enum class ERadarTargetDomain : uint8
{
	/** Airborne target (airplanes, helicopters, UAVs) */
	Air UMETA(DisplayName = "Air"),

	/** Ground surface target (vehicles, tanks, mobile SAM launchers, static structures) */
	Ground UMETA(DisplayName = "Ground"),

	/** Maritime surface target (ships, patrol boats, aircraft carriers) */
	Sea UMETA(DisplayName = "Sea / Maritime"),

	/** Guided missile (active radar homing seeker, cruise missile, surface-to-air missile in flight) */
	Missile UMETA(DisplayName = "Missile")
};

/**
 * Threat classification level detected by the Radar Warning Receiver (RWR)
 */
UENUM(BlueprintType)
enum class ERWRThreatType : uint8
{
	None UMETA(DisplayName = "No Threat"),
	FriendlyRadar UMETA(DisplayName = "Friendly Radar (IFF Cleared)"),
	SearchRadar UMETA(DisplayName = "Search Radar"),
	TrackingRadar UMETA(DisplayName = "Tracking Radar (TWS)"),
	LockOnRadar UMETA(DisplayName = "Lock-On Radar (STT)"),
	MissileSeeker UMETA(DisplayName = "Missile Seeker (Active Radar)"),
	MissileLaunch UMETA(DisplayName = "Missile Launch Detected")
};
