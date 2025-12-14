// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "AircraftCombatCommonTypes.generated.h"

UENUM(BlueprintType)
enum class EWeaponComponentType : uint8
{
	Unknown,
	IRMissile,
	RadarMissile,
	AntiRadiationMissile,
	DroppableStore
};

UENUM(BlueprintType)
enum class EWeaponSeekerState : uint8
{
	Standby,
	Caged,
	Slaved,
	Tracking,
	Lost
};

UENUM(BlueprintType)
enum class EWeaponAudioTone : uint8
{
	Silent,
	Searching,
	TrackingCandidate,
	Locked
};

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

/**
 * Detailed diagnostic reason for weapon launch failure or release inhibition
 */
UENUM(BlueprintType)
enum class EWeaponLaunchFailureReason : uint8
{
	/** Weapon release succeeded or station is ready to fire */
	None UMETA(DisplayName = "None / Success"),

	/** Release inhibited: Master Arm is set to SAFE */
	MasterArmSafe UMETA(DisplayName = "Master Arm is SAFE"),

	/** Requested station index does not exist */
	StationNotFound UMETA(DisplayName = "Station Not Found"),

	/** Station has no weapon class, mesh, or muzzles configured */
	NoWeaponConfigured UMETA(DisplayName = "No Weapon Configured"),

	/** Station ammunition or ready store inventory is exhausted */
	AmmoDepleted UMETA(DisplayName = "Station Ammunition Depleted"),

	/** Station is damaged or in a fault state */
	StationFault UMETA(DisplayName = "Station Fault / Damaged"),

	/** Station or store was jettisoned */
	StationJettisoned UMETA(DisplayName = "Station Jettisoned"),

	/** Weapon system is powered off or not initialized */
	WeaponNotReady UMETA(DisplayName = "Weapon System Not Ready"),

	/** Release inhibited: selected weapon cue is classified as friendly */
	FriendlyTargetInhibit UMETA(DisplayName = "Friendly Target - Release Inhibited"),

	/** Weapon requires a hard target lock before firing */
	TargetLockRequired UMETA(DisplayName = "Target Lock Required"),

	/** Semi-active radar missile requires continuous wave (CW) radar illumination from host or donor radar */
	NoContinuousWaveIllumination UMETA(DisplayName = "Continuous Wave (CW) Illumination Required"),

	/** Selected weapon cue is outside the missile dynamic launch zone (DLZ) */
	TargetOutOfEnvelope UMETA(DisplayName = "Target Outside Weapon Firing Envelope"),

	/** Target line-of-sight angle exceeds the seeker gimbal limit */
	SeekerGimbalLimitExceeded UMETA(DisplayName = "Target Beyond Seeker Gimbal Limit"),

	/** Remote datalink track or donor radar support is unavailable */
	DatalinkSupportUnavailable UMETA(DisplayName = "DataLink Support Unavailable"),

	/** Physical release mechanism or detachment failed */
	ReleaseMechanismFailed UMETA(DisplayName = "Pylon Release Mechanism Failed"),

	/** Failed to dynamically instantiate store actor */
	StoreSpawnFailed UMETA(DisplayName = "Store Actor Instantiation Failed"),

	/** Weapon store type is incompatible with the requested target track or engagement mode */
	InvalidStoreType UMETA(DisplayName = "Store Type Incompatible")
};
