// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"
#include "CombatTeamUtility.h"
#include "AircraftCombatCommonTypes.h"
#include "MasterWeaponComponent.h"
#include "Net/UnrealNetwork.h"
#include "ModularMissionManagement.generated.h"

class AActor;
class APawn;
class USceneComponent;
class AWeapon;
class UAircraftRadarComponent;
class UMissileGuidanceComponent;
class URadarMissileGuidanceComponent;
class UIRMissileGuidanceComponent;
class UARMMissileGuidanceComponent;
class UDroppableItemComponent;
class URadarWarningReceiverComponent;
struct FRadarTrack;

/**
 * Categorization of stores and weapons loaded on aircraft stations
 */
UENUM(BlueprintType)
enum class EStoreType : uint8
{
	None UMETA(DisplayName = "None / Empty"),
	AirToAirMissile_IR UMETA(DisplayName = "Air-to-Air Missile (IR)"),
	AirToAirMissile_Radar UMETA(DisplayName = "Air-to-Air Missile (Radar)"),
	AirToGroundMissile UMETA(DisplayName = "Air-to-Ground Missile"),
	AntiRadiationMissile UMETA(DisplayName = "Anti-Radiation Missile (ARM/SEAD)"),
	Bomb_Unguided UMETA(DisplayName = "Unguided Bomb"),
	Bomb_Guided UMETA(DisplayName = "Guided Bomb"),
	RocketPod UMETA(DisplayName = "Rocket Pod"),
	GunPod UMETA(DisplayName = "Gun Pod"),
	FuelTank UMETA(DisplayName = "External Fuel Tank"),
	TargetingPod UMETA(DisplayName = "Targeting / Sensor Pod"),
	CountermeasurePod UMETA(DisplayName = "Countermeasure / ECM Pod"),
	InternalCannon UMETA(DisplayName = "Built-in Cannon / Gun")
};

/**
 * Operational status of a hardpoint station
 */
UENUM(BlueprintType)
enum class EStationStatus : uint8
{
	Empty UMETA(DisplayName = "Empty"),
	Ready UMETA(DisplayName = "Ready"),
	Selected UMETA(DisplayName = "Selected"),
	Firing UMETA(DisplayName = "Firing"),
	Fault UMETA(DisplayName = "Fault / Damaged"),
	Jettisoned UMETA(DisplayName = "Jettisoned")
};

/**
 * Operational status of an individual store/weapon
 */
UENUM(BlueprintType)
enum class EStoreStatus : uint8
{
	Ready UMETA(DisplayName = "Ready"),
	Fired UMETA(DisplayName = "Fired / Empty"),
	Fault UMETA(DisplayName = "Fault / Damaged")
};

/**
 * Aircraft Master Arm safety states
 */
UENUM(BlueprintType)
enum class EMasterArmMode : uint8
{
	Safe UMETA(DisplayName = "SAFE"),
	Arm UMETA(DisplayName = "ARM"),
	Simulate UMETA(DisplayName = "SIMULATE")
};

/**
 * Master combat delivery mode
 */
UENUM(BlueprintType)
enum class EAircraftMasterMode : uint8
{
	Navigation UMETA(DisplayName = "Navigation"),
	AirToAir UMETA(DisplayName = "Air To Air"),
	AirToGround UMETA(DisplayName = "Air To Ground"),
	Dogfight UMETA(DisplayName = "Dogfight Mode"),
	MissileOverride UMETA(DisplayName = "Missile Override Mode"),
	Emergency UMETA(DisplayName = "Emergency")
};

/**
 * Configuration for a static mesh pylon to be attached to a station
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FStationPylon
{
	GENERATED_BODY()

	/** The static mesh representing the pylon */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pylon")
	TObjectPtr<UStaticMesh> PylonMesh = nullptr;

	/** Name of the socket on the aircraft mesh where this pylon should be attached (grayed out until a PylonMesh is assigned) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pylon", meta = (EditCondition = "PylonMesh != nullptr"))
	FName SocketName = NAME_None;
};

/**
 * Firing pattern for built-in cannon stations with multiple muzzles
 */
UENUM(BlueprintType)
enum class EGunFiringPattern : uint8
{
	Simultaneous UMETA(DisplayName = "Simultaneous (All Muzzles Fire Together)"),
	Alternating UMETA(DisplayName = "Alternating (Round-Robin Between Muzzles)")
};

/**
 * Definition of an individual gun barrel / muzzle for a built-in cannon station
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FGunMuzzle
{
	GENERATED_BODY()

	/** Name of the socket on the aircraft mesh where this specific muzzle is located */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon")
	FName MuzzleSocketName = NAME_None;

	/** Optional descriptive name (e.g. "Port Cannon", "Starboard Cannon") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon")
	FText MuzzleName;

	/** Muzzle exit speed (cm/s). Default 105,000 cm/s = 1,050 m/s (~M61 Vulcan round) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Ballistics", meta = (ClampMin = "1000.0"))
	float BulletSpeed = 105000.0f;

	/** Lifespan of bullet before expiring (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Ballistics", meta = (ClampMin = "0.5"))
	float BulletLifespan = 4.0f;

	/** Gravity scale multiplier (1.0 = standard Earth gravity, 0.0 = no drop) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Ballistics", meta = (ClampMin = "0.0"))
	float GravityScale = 1.0f;

	/** Aerodynamic drag coefficient decelerating the bullet over distance (0.0 = vacuum/no drag) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Ballistics", meta = (ClampMin = "0.0"))
	float DragCoefficient = 0.00002f;

	/** Trace collision radius (0.0 = line trace, >0 = swept sphere for round caliber thickness e.g. 2.0 = 20mm shell) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Collision", meta = (ClampMin = "0.0"))
	float TraceRadius = 0.0f;

	/** Collision channel used for bullet raycast detection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Collision")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
};

/**
 * Internal lightweight active swept-trace bullet particle
 */
USTRUCT()
struct FActiveTracedBullet
{
	GENERATED_BODY()

	UPROPERTY()
	FVector CurrentPosition = FVector::ZeroVector;

	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY()
	float RemainingLifetime = 0.0f;

	UPROPERTY()
	float GravityScale = 1.0f;

	UPROPERTY()
	float DragCoefficient = 0.0f;

	UPROPERTY()
	float TraceRadius = 0.0f;

	UPROPERTY()
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY()
	int32 StationIndex = 0;

	UPROPERTY()
	int32 MuzzleIndex = 0;
};

/**
 * Definition and runtime state of an individual weapon on a station/rack
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FStationStore
{
	GENERATED_BODY()

	/** Actor class to spawn for this weapon/store */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Store")
	TSubclassOf<AActor> WeaponClass = nullptr;

	/** User-friendly display name of the weapon (e.g. "AIM-9X Sidewinder") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Store")
	FText DisplayName;

	/** Name of the socket on the aircraft mesh or pylon where this specific store is attached (grayed out until WeaponClass is assigned) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Store", meta = (EditCondition = "WeaponClass != nullptr"))
	FName SocketName = NAME_None;

	/** Operational status of this specific store */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Store|Runtime")
	EStoreStatus Status = EStoreStatus::Ready;

	/** Reference to the spawned/attached store actor */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Store|Runtime")
	TObjectPtr<AActor> MountedActor = nullptr;
};

/**
 * Definition and runtime state of an individual weapon station / pylon hardpoint
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FWeaponStation
{
	GENERATED_BODY()

	/** Unique station index / hardpoint number (e.g. 1 to 9) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station", meta = (ClampMin = "1"))
	int32 StationIndex = 1;

	/** Descriptive name or station designation (e.g. "STA 1 - L WINGTIP") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station")
	FText StationName;

	/** Primary store category installed on this station */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Store")
	EStoreType StoreType = EStoreType::None;

	/** Operational status of this station */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Station|Runtime")
	EStationStatus Status = EStationStatus::Ready;

	/** If true, this station acts as a persistent launcher/pod (e.g. Gun Pod, Rocket Pod) where the mounted store remains attached and fires internal ammunition rounds. If false, each round is a discrete physical store (e.g. missile, bomb, drop tank) that detaches upon release. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Store", meta = (EditCondition = "StoreType != EStoreType::None && StoreType != EStoreType::InternalCannon", EditConditionHides))
	bool bUsesAmmoManagement = false;

	/** List of individual weapons loaded on this station (e.g., 3 missiles on a TER rack, or 1 persistent gun pod) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Store", meta = (EditCondition = "StoreType != EStoreType::None && StoreType != EStoreType::InternalCannon", EditConditionHides))
	TArray<FStationStore> Stores;

	/** Maximum ammunition count for ammo-based weapons */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Store", meta = (EditCondition = "(StoreType == EStoreType::InternalCannon) || (StoreType != EStoreType::None && bUsesAmmoManagement)", EditConditionHides, ClampMin = "1"))
	int32 MaxAmmo = 1;

	/** Current ammunition count for ammo-based weapons */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Store", meta = (EditCondition = "(StoreType == EStoreType::InternalCannon) || (StoreType != EStoreType::None && bUsesAmmoManagement)", EditConditionHides, ClampMin = "0"))
	int32 CurrentAmmo = 1;

	/** Dry-fire rounds remaining in Simulate mode; live ammunition is never consumed by simulation. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Cannon|Runtime")
	int32 SimulatedAmmo = 1;

	/** List of gun muzzles/barrels configured for this built-in cannon station */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (EditCondition = "StoreType == EStoreType::InternalCannon", EditConditionHides))
	TArray<FGunMuzzle> GunMuzzles;

	/** Firing pattern for multi-muzzle cannon stations (Simultaneous vs Alternating) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (EditCondition = "StoreType == EStoreType::InternalCannon", EditConditionHides))
	EGunFiringPattern GunFiringPattern = EGunFiringPattern::Simultaneous;

	/** Rate of fire in Rounds Per Minute (RPM), e.g. 6000 for M61 Vulcan, 3900 for GAU-8 Avenger */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (EditCondition = "StoreType == EStoreType::InternalCannon", EditConditionHides, ClampMin = "60.0"))
	float RateOfFireRPM = 6000.0f;

	/** Bullet angular dispersion/spread in degrees (0.0 = pinpoint socket accuracy, >0 = dispersion cone) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (EditCondition = "StoreType == EStoreType::InternalCannon", EditConditionHides, ClampMin = "0.0", ClampMax = "10.0"))
	float BulletSpreadDegrees = 0.0f;

	/** Helper to count remaining ready stores or ammo */
	int32 GetCurrentAmmo() const
	{
		if (StoreType == EStoreType::None || Status == EStationStatus::Jettisoned) return 0;
		if (StoreType == EStoreType::InternalCannon || bUsesAmmoManagement)
		{
			return FMath::Max(0, CurrentAmmo);
		}

		int32 Count = 0;
		for (const FStationStore& Store : Stores)
		{
			if (Store.Status == EStoreStatus::Ready) Count++;
		}
		return Count;
	}

	/** Helper to count max capacity */
	int32 GetMaxAmmo() const
	{
		if (StoreType == EStoreType::InternalCannon || bUsesAmmoManagement)
		{
			return FMath::Max(0, MaxAmmo);
		}
		return Stores.Num();
	}

	/** If true, this store can be jettisoned (e.g. drop tanks, bombs, missiles) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jettison", meta = (EditCondition = "StoreType != EStoreType::None && StoreType != EStoreType::InternalCannon", EditConditionHides))
	bool bCanJettison = true;

	/** Downward/outward impulse vector applied to detached store when released or jettisoned */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jettison", meta = (EditCondition = "StoreType != EStoreType::None && StoreType != EStoreType::InternalCannon && (!bUsesAmmoManagement || bCanJettison)", EditConditionHides))
	FVector EjectionImpulse = FVector(0.0f, 0.0f, -500.0f);

	/** Index of symmetrical station on opposite wing for paired firing and balance (-1 if none/centerline) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station", meta = (EditCondition = "StoreType != EStoreType::None && StoreType != EStoreType::InternalCannon", EditConditionHides))
	int32 SymmetricStationIndex = -1;

	/** Array of static mesh pylons/adapters to spawn for this station */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pylon")
	TArray<FStationPylon> Pylons;

	/** Transient references to the spawned pylon static mesh components */
	UPROPERTY(Transient, NotReplicated)
	TArray<TObjectPtr<UStaticMeshComponent>> SpawnedPylons;
};

/**
 * Network replicated firing state for continuous gun firing
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FGunFiringState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cannon")
	int32 StationIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cannon")
	bool bIsFiring = false;
};

// Delegate declarations
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMasterArmChangedSignature, EMasterArmMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMasterModeChangedSignature, EAircraftMasterMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStationSelectedSignature, int32, StationIndex, const FWeaponStation&, StationData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnStationWeaponFiredSignature, int32, StationIndex, AActor*, SpawnedWeapon, EStoreType, StoreType, int32, RemainingAmmo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponJettisonedSignature, int32, StationIndex, EStoreType, StoreType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStoresInventoryChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnStationAmmoChangedSignature, int32, StationIndex, int32, NewAmmo, int32, MaxAmmo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStationStatusChangedSignature, int32, StationIndex, EStationStatus, NewStatus);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponLaunchFailedSignature, int32, StationIndex, EWeaponLaunchFailureReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWeaponLaunchResultSignature, int32, StationIndex, bool, bSucceeded, EWeaponLaunchFailureReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionTargetLockedSignature, AActor*, TargetActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMissionLockClearedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionTargetBuggedSignature, AActor*, TargetActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMissionBugClearedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGunFiringStartedSignature, int32, StationIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGunFiringStoppedSignature, int32, StationIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGunBulletHitSignature, int32, StationIndex, int32, MuzzleIndex, const FHitResult&, HitResult);

/**
 * Modular Mission Management Component
 * Manages aircraft weapon stations, pylons, store inventories, Master Arm states,
 * weapon release sequences, missile target handoff, and selective/emergency jettisons.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UModularMissionManagement : public UActorComponent
{
	GENERATED_BODY()

public:
	UModularMissionManagement();


	/** Configured navigation mark points */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|MarkPoints")
	TArray<FVector> MarkPoints;

	/** Index of the currently active navigation mark point */
	UPROPERTY(BlueprintReadWrite, Category = "Navigation|MarkPoints")
	int32 ActiveMarkPointIndex = 0;

	/** Event triggered when Master Arm mode changes (Safe / Arm / Simulate) */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnMasterArmChangedSignature OnMasterArmChanged;

	/** Event triggered when aircraft master combat mode changes (NAV / A-A / A-G / Emergency) */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnMasterModeChangedSignature OnMasterModeChanged;

	/** Event triggered when active weapon station selection changes */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnStationSelectedSignature OnStationSelected;

	/** Event triggered when a weapon or store is successfully fired/released */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnStationWeaponFiredSignature OnWeaponFired;

	/** Event triggered when a store is jettisoned from a station */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnWeaponJettisonedSignature OnWeaponJettisoned;

	/** Event triggered when stores inventory counts or loaded configurations change */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnStoresInventoryChangedSignature OnStoresInventoryChanged;

	/** Event triggered when an individual station's ammunition count changes */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnStationAmmoChangedSignature OnStationAmmoChanged;

	/** Event triggered when an individual station's operational status changes (Ready, Selected, Fault, Empty, Jettisoned) */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnStationStatusChangedSignature OnStationStatusChanged;

	/** Event triggered when a launch attempt fails safety or prerequisite checks */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnWeaponLaunchFailedSignature OnWeaponLaunchFailed;

	/** Authoritative response to a client Fire request. */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnWeaponLaunchResultSignature OnWeaponLaunchResult;

	/** Event triggered when a hard lock is assigned */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnMissionTargetLockedSignature OnTargetLocked;

	/** Event triggered when the locked target is cleared/deselected */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnMissionLockClearedSignature OnLockCleared;
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnMissionTargetBuggedSignature OnTargetBugged;
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnMissionBugClearedSignature OnBugCleared;

	/** Event triggered when a continuous gun begins firing */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnGunFiringStartedSignature OnGunFiringStarted;

	/** Event triggered when a continuous gun stops firing */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnGunFiringStoppedSignature OnGunFiringStopped;

	/** Event triggered when a swept-trace cannon bullet hits an object in the world */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnGunBulletHitSignature OnGunBulletHit;

	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnWeaponSeekerStateChangedSignature OnActiveWeaponSeekerStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnWeaponLockAcquiredSignature OnActiveWeaponLockAcquired;
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnWeaponLockLostSignature OnActiveWeaponLockLost;
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnWeaponCageStateChangedSignature OnActiveWeaponCageStateChanged;

	/** Array of hardpoint weapon stations configured on this aircraft */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Stations, Category = "Mission Management|Stations")
	TArray<FWeaponStation> Stations;

	/** Current Master Arm switch state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MasterArmMode, Category = "Mission Management|Control")
	EMasterArmMode MasterArmMode = EMasterArmMode::Safe;

	/** Current aircraft combat master mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MasterMode, Category = "Mission Management|Control")
	EAircraftMasterMode MasterMode = EAircraftMasterMode::AirToAir;

	/** If true, switching master combat mode automatically selects the primary station for that mode (e.g. Dogfight -> Cannon, MissileOverride -> BVR) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission Management|Control")
	bool bAutoSelectStationOnMasterMode = true;


	/** If true, automatically spawns and attaches store visual actors to aircraft mesh sockets at BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission Management|Initialization")
	bool bAutoSpawnStoresOnBeginPlay = true;

	/** If true, automatically alternates to symmetrical or next valid station after firing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission Management|Control")
	bool bAutoStepStationOnFire = true;

	/** If true, weapon release is inhibited when its cue or the IR seeker's own lock is Friendly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Mission Management|Safety")
	bool bInhibitFriendlyFire = false;

	/** Network replicated gun firing state for visual and audio synchronization across clients */
	UPROPERTY(Transient, ReplicatedUsing = OnRep_GunFiringState)
	FGunFiringState GunFiringState;

	UFUNCTION()
	void OnRep_MasterArmMode();

	UFUNCTION()
	void OnRep_MasterMode();

	UFUNCTION()
	void OnRep_Stations();

	UFUNCTION()
	void OnRep_GunFiringState();

	/** If true, automatically programs all weapons to ignore the parent aircraft, pylons, and all other mounted stores */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission Management|Weapons")
	bool bConfigureWeaponsToIgnoreAircraftAndStores = true;

	/** If true, renders visual debug lines and impact markers for swept-trace cannon bullets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission Management|Debug")
	bool bDebug = false;

	/**
	 * Configures bidirectional collision and movement ignores between a store actor,
	 * the parent aircraft, all spawned pylons, and all other mounted stores.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Weapons")
	void SetupStoreCollisionIgnores(AActor* StoreActor);

	/**
	 * Programs the specified weapon component with an ignore list containing the parent aircraft and all mounted stores.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Weapons")
	void ProgramWeaponIgnoreList(class UMasterWeaponComponent* WeaponComp);

	/**
	 * Programs all weapon components across all stations with the full aircraft/stores ignore list.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Weapons")
	void ProgramAllWeaponsIgnoreLists();

	/**
	 * Initializes stations, finds the parent aircraft mesh, and mounts stores.
	 *
	 * @param InAircraftMesh Optional scene component representing aircraft mesh to attach pylons/stores
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Setup")
	void InitializeStores(USceneComponent* InAircraftMesh = nullptr);

	/**
	 * Spawns and attaches store actors for a specific station.
	 *
	 * @param StationIndex Target station index
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Setup")
	void SpawnStoreForStation(int32 StationIndex);

	/**
	 * Spawns and attaches visual store actors for all configured stations.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Setup")
	void SpawnAllStores();

	/**
	 * Cleans up and destroys all currently mounted store actors.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Setup")
	void DestroyMountedStores();

	/**
	 * Spawns and attaches all static mesh pylons configured on all stations.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Setup")
	void SpawnAllPylons();

	/**
	 * Cleans up and destroys all spawned pylon components.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Setup")
	void DestroyAllPylons();

	/** Returns all configured weapon stations */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Inventory")
	const TArray<FWeaponStation>& GetStations() const { return Stations; }

	/** Retrieves station data for a given station index */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Inventory")
	bool GetStation(int32 StationIndex, FWeaponStation& OutStation) const;

	/** Retrieves station data by pylon socket name */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Inventory")
	bool GetStationBySocket(FName SocketName, FWeaponStation& OutStation) const;

	/** Returns all stations matching a specific store type */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Inventory")
	void GetStationsByStoreType(EStoreType InStoreType, TArray<FWeaponStation>& OutStations) const;

	/** Returns total remaining ammo count for a specific store type across all stations */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Inventory")
	int32 GetTotalAmmoForStoreType(EStoreType InStoreType) const;

	/** Returns total remaining ammunition across all weapon stations */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Inventory")
	int32 GetTotalAmmoCount() const;

	/** Returns the total number of configured stations */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Inventory")
	int32 GetStationCount() const { return Stations.Num(); }

	/**
	 * Selects an active station by index.
	 *
	 * @param StationIndex Station index to activate
	 * @return True if station was valid and selected
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Selection")
	bool SelectStation(int32 StationIndex);

	/** Selects the next available non-empty station */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Selection")
	bool SelectNextStation();

	/** Selects the previous available non-empty station */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Selection")
	bool SelectPreviousStation();

	/** Selects the next station loaded with the specified store type */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Selection")
	bool SelectNextStationOfStoreType(EStoreType InStoreType);

	/** Cycles through available store types (e.g. IR Missile -> Radar Missile -> Bombs) */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Selection")
	bool CycleWeaponType();

	/** Selects the first available station loaded with the given store type */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Selection")
	bool SelectStoreType(EStoreType InStoreType);

	/** Returns the currently selected station index */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Selection")
	FORCEINLINE int32 GetSelectedStationIndex() const { return SelectedStationIndex; }

	/** Retrieves the currently selected station data */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Selection")
	bool GetSelectedStation(FWeaponStation& OutStation) const;

	/** Retrieves the active weapon store struct (WeaponClass, DisplayName, SocketName, etc.) on the currently selected station */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Selection")
	bool GetActiveStore(FStationStore& OutStore) const;

	/** Retrieves the active weapon store struct on a specified station */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Selection")
	bool GetActiveStoreForStation(int32 StationIndex, FStationStore& OutStore) const;

	/** Returns the store type of the currently selected station */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Selection")
	EStoreType GetSelectedStoreType() const;

	/** Gets the active weapon actor (next to be fired) on the specified station */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Selection")
	AActor* GetActiveWeaponActor(int32 StationIndex) const;

	/** Gets the active weapon component (next to be fired) on the specified station */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Selection")
	class UMasterWeaponComponent* GetActiveWeaponComponent(int32 StationIndex) const;

	UFUNCTION(BlueprintPure, Category = "Mission Management|Weapons")
	EWeaponComponentType GetSelectedWeaponComponentType() const;
	UFUNCTION(BlueprintPure, Category = "Mission Management|Weapons")
	UMissileGuidanceComponent* GetSelectedMissileGuidance() const;
	UFUNCTION(BlueprintPure, Category = "Mission Management|Weapons")
	UIRMissileGuidanceComponent* GetSelectedIRMissile() const;
	UFUNCTION(BlueprintPure, Category = "Mission Management|Weapons")
	URadarMissileGuidanceComponent* GetSelectedRadarMissile() const;
	UFUNCTION(BlueprintPure, Category = "Mission Management|Weapons")
	UARMMissileGuidanceComponent* GetSelectedARMMissile() const;
	UFUNCTION(BlueprintPure, Category = "Mission Management|Weapons")
	UDroppableItemComponent* GetSelectedDroppableItem() const;

	UFUNCTION(BlueprintCallable, Category = "Mission Management|Weapons")
	bool SlaveSelectedWeaponToDirection(const FVector& InWorldDirection);
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Weapons")
	bool SlaveSelectedWeaponToLocation(const FVector& InWorldLocation);
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Weapons")
	void SlaveSelectedWeaponToBoresight();
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Weapons")
	void SetSelectedWeaponCaged(bool bCaged);

	/** Sets Master Arm mode (Safe / Arm / Simulate) */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Control")
	void SetMasterArmMode(EMasterArmMode InMode);

	/** Returns the current Master Arm mode */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Control")
	FORCEINLINE EMasterArmMode GetMasterArmMode() const { return MasterArmMode; }

	/** Returns true if Master Arm is in ARM state */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Control")
	FORCEINLINE bool IsMasterArmArmed() const { return MasterArmMode == EMasterArmMode::Arm; }

	/** Sets the aircraft combat master mode */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Control")
	void SetMasterMode(EAircraftMasterMode InMode);

	/** Returns the current combat master mode */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Control")
	FORCEINLINE EAircraftMasterMode GetMasterMode() const { return MasterMode; }

	/** Assigns the hard-locked radar target for weapon handoff */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Targeting")
	void LockActor(AActor* InTarget);
	/** Called by the radar after a server-validated linked track selection. */
	void PrepareLinkedRadarWeapon(const FRadarTrack& LinkedTrack, UAircraftRadarComponent* SourceRadar);

	/** Returns the currently locked target actor (or nullptr if invalid/destroyed) */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Targeting")
	AActor* GetLockedActor() const;
	UFUNCTION(BlueprintPure, Category = "Mission Management|Release")
	EWeaponLaunchFailureReason GetLastLaunchFailureReason() const { return LastLaunchFailureReason; }

	/** Clears the locked target */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Targeting")
	void ClearLockedActor();
	/** Assigns the TWS priority target used for seeker cueing. This does not imply illumination. */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Targeting")
	void BugActor(AActor* InTarget);
	UFUNCTION(BlueprintPure, Category = "Mission Management|Targeting")
	AActor* GetBuggedActor() const { return IsValid(BuggedTarget) ? BuggedTarget.Get() : nullptr; }
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Targeting")
	void ClearBuggedActor();

	/** Auto-discovers and caches the UAircraftRadarComponent on the owner pawn */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Radar")
	UAircraftRadarComponent* ResolveRadarComponent();

	/** Returns the cached aircraft radar component (or attempts to find it if null) */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Radar")
	UAircraftRadarComponent* GetRadarComponent() const;

	/** Auto-discovers and caches the URadarWarningReceiverComponent on the owner pawn */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|RWR")
	URadarWarningReceiverComponent* ResolveRWRComponent();

	/** Returns the cached aircraft RWR component (or attempts to find it if null) */
	UFUNCTION(BlueprintPure, Category = "Mission Management|RWR")
	URadarWarningReceiverComponent* GetRWRComponent() const;

	/** Calculates the Dynamic Launch Zone (DLZ: Rmin, Rne, Rmax) against the selected weapon cue */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Radar")
	bool CalculateMissileLaunchZone(int32 StationIndex, float& OutRmin, float& OutRne, float& OutRmax, bool& OutInShootingEnvelope) const;

	/**
	 * Computes a radar-integrated firing lead solution for internal cannons or rocket stations.
	 * Uses filtered radar track distance, closure rate, and target velocity combined with muzzle ballistics.
	 *
	 * @param StationIndex Station index of the weapon (must be InternalCannon or RocketPod)
	 * @param OutLeadLocation World space aim location for HUD lead pipper
	 * @param OutTimeOfFlight Calculated bullet time of flight in seconds
	 * @return True if a valid firing solution was computed
	 */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Radar")
	bool GetGunLeadSolution(int32 StationIndex, FVector& OutLeadLocation, float& OutTimeOfFlight) const;

	/**
	 * Returns the primary radar target track (STT lock or priority TWS bug) if available.
	 *
	 * @param OutTrack Target track information populated from radar
	 * @return True if a valid locked or bugged track was found
	 */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Radar")
	bool GetPrimaryRadarTarget(FRadarTrack& OutTrack) const;

	/** Automatically selects the best dogfight station (Internal Cannon or Short-Range AAM) */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Stations")
	bool SelectDogfightStation();

	/** Automatically selects the best Beyond-Visual-Range (BVR) radar missile station */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Stations")
	bool SelectBVRStation();

	/**
	 * Checks if a specific station is authorized and ready to fire.
	 *
	 * @param StationIndex Target station index
	 * @param OutFailReason Text description if launch conditions are not met
	 * @return True if station is ready to fire
	 */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Release")
	bool CanFire(int32 StationIndex, EWeaponLaunchFailureReason& OutFailReason) const;

	/**
	 * Fires/releases store from a specific station index.
	 *
	 * @param StationIndex Target station index to release
	 * @return True if firing was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Release")
	bool Fire(int32 StationIndex);

	/** Routes a mounted weapon's direct request through its actual station safety checks. */
	bool FireMountedWeapon(const UMasterWeaponComponent* Weapon);

	/**
	 * Begins continuous firing for a built-in cannon station.
	 * Discharges rounds at the configured RateOfFireRPM until StopFiring is called or ammo depletes.
	 *
	 * @param StationIndex Target gun station index
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Release")
	void StartFiring(int32 StationIndex);

	/**
	 * Stops continuous firing for a built-in cannon station.
	 *
	 * @param StationIndex Target gun station index
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Release")
	void StopFiring(int32 StationIndex);

	/** Begins continuous firing on the currently selected station */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Release")
	void StartFiringActive();

	/** Stops continuous firing on the currently selected station */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Release")
	void StopFiringActive();

	/** Returns true if the specified station is currently actively firing */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Release")
	bool IsStationFiring(int32 StationIndex) const;

	/** Returns true if any gun station on the aircraft is currently firing */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Release")
	bool IsAnyGunFiring() const;

	/**
	 * Jettisons all external stores and drop tanks marked as bCanJettison immediately.
	 *
	 * @return Total number of stores successfully jettisoned
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Jettison")
	int32 EmergencyJettisonAll();

	/**
	 * Jettisons store from a specific station index.
	 *
	 * @param StationIndex Station to jettison
	 * @return True if store was successfully jettisoned
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Jettison")
	bool SelectiveJettisonStation(int32 StationIndex);

	/**
	 * Jettisons all stores matching the specified type (e.g. drop fuel tanks).
	 *
	 * @param InStoreType Store category to drop
	 * @return Number of stores jettisoned
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Jettison")
	int32 JettisonByType(EStoreType InStoreType);

	/** Reconfigures an existing station */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Inventory")
	bool ConfigureStation(int32 StationIndex, const FWeaponStation& NewStationConfig);

	/** Reloads ammunition for a specific station */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Inventory")
	bool ReloadStation(int32 StationIndex, int32 AmmoCount);

	/** Restores all configured stations to full MaxAmmo capacity and respawns stores */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Inventory")
	void ReloadAllStations();

	// Server RPCs for multiplayer client prediction and authority execution
	UFUNCTION(Server, Reliable, Category = "Mission Management|Control")
	void ServerSetMasterArmMode(EMasterArmMode InMode);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Control")
	void ServerSetMasterMode(EAircraftMasterMode InMode);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Selection")
	void ServerSelectStation(int32 StationIndex);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Selection")
	void ServerSelectNextStation();

	UFUNCTION(Server, Reliable, Category = "Mission Management|Selection")
	void ServerSelectPreviousStation();

	UFUNCTION(Server, Reliable, Category = "Mission Management|Selection")
	void ServerSelectNextStationOfStoreType(EStoreType InStoreType);
	UFUNCTION(Server, Reliable, Category = "Mission Management|Weapons")
	void ServerSlaveSelectedWeaponToDirection(FVector InWorldDirection);
	UFUNCTION(Server, Reliable, Category = "Mission Management|Weapons")
	void ServerSlaveSelectedWeaponToLocation(FVector InWorldLocation);
	UFUNCTION(Server, Reliable, Category = "Mission Management|Weapons")
	void ServerSlaveSelectedWeaponToBoresight();
	UFUNCTION(Server, Reliable, Category = "Mission Management|Weapons")
	void ServerSetSelectedWeaponCaged(bool bCaged);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Targeting")
	void ServerLockActor(AActor* InTarget);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Targeting")
	void ServerBugActor(AActor* InTarget);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Release")
	void ServerFire(int32 StationIndex);

	UFUNCTION(Client, Reliable, Category = "Mission Management|Release")
	void ClientFireResult(int32 StationIndex, bool bSucceeded, EWeaponLaunchFailureReason Reason);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Release")
	void ServerStartFiring(int32 StationIndex);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Release")
	void ServerStopFiring(int32 StationIndex);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Jettison")
	void ServerEmergencyJettisonAll();

	UFUNCTION(Server, Reliable, Category = "Mission Management|Jettison")
	void ServerSelectiveJettisonStation(int32 StationIndex);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Jettison")
	void ServerJettisonByType(EStoreType InStoreType);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Finds the target scene component / mesh on owner pawn to attach pylons */
	USceneComponent* ResolveAircraftMesh() const;

	/** Internal helper to release an individual store actor physically, populating OutFailReason on rejection */
	AActor* ExecuteStoreRelease(FWeaponStation& Station, AActor* TargetActor, EWeaponLaunchFailureReason& OutFailReason);

	/** Prepares, activates, and slaves the active weapon on a station to the current target and radar */
	void PrepareActiveWeaponOnStation(int32 StationIndex);
	void RefreshSelectedWeaponCue();
	void BindActiveWeaponDelegates();
	void UnbindActiveWeaponDelegates();
	UFUNCTION() void HandleActiveWeaponSeekerStateChanged(UMasterWeaponComponent* Weapon, EWeaponSeekerState OldState, EWeaponSeekerState NewState);
	UFUNCTION() void HandleActiveWeaponLockAcquired(UMasterWeaponComponent* Weapon, AActor* Target);
	UFUNCTION() void HandleActiveWeaponLockLost(UMasterWeaponComponent* Weapon, AActor* Target);
	UFUNCTION() void HandleActiveWeaponCageStateChanged(UMasterWeaponComponent* Weapon, bool bCaged);
	AActor* GetCueTargetForWeapon(const UMasterWeaponComponent* Weapon) const;
	bool IsWeaponReleaseInhibitedByIFF(const UMasterWeaponComponent* Weapon, const AActor* CueTarget) const;
	UFUNCTION() void HandleRadarLockAcquired(const FRadarTrack& Track);
	UFUNCTION() void HandleRadarLockLost(int32 TrackID);
	UFUNCTION() void HandleRadarTrackSelected(const FRadarTrack& Track);
	UFUNCTION() void HandleRadarTrackDeselected(int32 TrackID);

	/** Internal callback fired when the locked target actor is destroyed */
	UFUNCTION()
	void HandleCueTargetDestroyed(AActor* DestroyedActor);

	/** Internal method executed on each timer tick to step cannon firing cycles. Returns true if round fired. */
	bool ProcessGunFireCycle(int32 StationIndex);

	/** Timer tick callback for internal cannon fire cycles */
	UFUNCTION()
	void HandleGunFireTimerTick(int32 StationIndex) { ProcessGunFireCycle(StationIndex); }

	/** Internal method executed on each frame to advance active swept-trace bullets */
	void SimulateTracedBullets(float DeltaTime);

	/** Active swept-trace ballistic bullets currently in flight */
	UPROPERTY(Transient)
	TArray<FActiveTracedBullet> ActiveTracedBullets;

	/** Active continuous firing timers mapped by StationIndex */
	UPROPERTY(Transient)
	TMap<int32, FTimerHandle> ActiveGunTimers;

	/** Round-robin muzzle tracking indices mapped by StationIndex */
	UPROPERTY(Transient)
	TMap<int32, int32> GunMuzzleIndices;

	/** Recorded launch failure reason from the most recent launch attempt */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mission Management|Runtime")
	EWeaponLaunchFailureReason LastLaunchFailureReason = EWeaponLaunchFailureReason::None;

	/** Index of the currently selected station */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_SelectedStationIndex, Category = "Mission Management|Runtime")
	int32 SelectedStationIndex = 1;

	UFUNCTION()
	void OnRep_SelectedStationIndex();

	/** Target actor held by a hard radar lock */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, ReplicatedUsing = OnRep_LockedTarget, Category = "Mission Management|Runtime")
	TObjectPtr<AActor> LockedTarget = nullptr;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, ReplicatedUsing = OnRep_BuggedTarget, Category = "Mission Management|Runtime")
	TObjectPtr<AActor> BuggedTarget = nullptr;

	UFUNCTION()
	void OnRep_LockedTarget();
	UFUNCTION()
	void OnRep_BuggedTarget();

	/** Cached pointer to the aircraft mesh component used for sockets */
	UPROPERTY(Transient)
	mutable TObjectPtr<USceneComponent> CachedAircraftMesh = nullptr;

	/** Cached pointer to the aircraft radar component if present on the aircraft */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mission Management|Runtime")
	TObjectPtr<UAircraftRadarComponent> CachedRadarComponent = nullptr;

	/** Cached pointer to the aircraft RWR component if present on the aircraft */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mission Management|Runtime")
	TObjectPtr<URadarWarningReceiverComponent> CachedRWRComponent = nullptr;

private:
	/** Local presentation components survive inventory replication independently. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LocalPylons;
	bool bStoreReleaseInProgress = false;
	TWeakObjectPtr<UMasterWeaponComponent> SubscribedActiveWeapon;
	/** Explicit pilot cue takes precedence until the target designation or selected station changes. */
	TWeakObjectPtr<UMasterWeaponComponent> PilotCuedWeapon;
	/** Tracks cues applied by the mission system so only those cues are cleared with a designation. */
	TWeakObjectPtr<UMasterWeaponComponent> AutomaticallyCuedWeapon;
	/** Finds index in Stations array for given station ID */
	int32 FindStationArrayIndex(int32 StationIndex) const;

};
