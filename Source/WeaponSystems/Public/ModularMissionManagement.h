// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"
#include "CombatTeamUtility.h"
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
 * Reason for weapon launch failure
 */
UENUM(BlueprintType)
enum class EWeaponLaunchFailureReason : uint8
{
	None UMETA(DisplayName = "None / Success"),
	MasterArmSafe UMETA(DisplayName = "Master Arm is SAFE"),
	StationNotFound UMETA(DisplayName = "Station Not Found"),
	NoWeaponConfigured UMETA(DisplayName = "No Weapon Configured"),
	AmmoDepleted UMETA(DisplayName = "Station Ammunition Depleted"),
	StationFault UMETA(DisplayName = "Station Fault / Damaged"),
	StationJettisoned UMETA(DisplayName = "Station Jettisoned"),
	WeaponNotReady UMETA(DisplayName = "Weapon System Not Ready"),
	FriendlyTargetInhibit UMETA(DisplayName = "Friendly Target - Release Inhibited")
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

	/** If true, this station acts as a single weapon with multiple rounds (e.g., Gun Pod) instead of a rack of disposable weapons. */
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
		if (StoreType == EStoreType::InternalCannon || bUsesAmmoManagement)
		{
			return CurrentAmmo;
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
			return MaxAmmo;
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
	UPROPERTY(Transient)
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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponLaunchFailedSignature, int32, StationIndex, EWeaponLaunchFailureReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWeaponLaunchResultSignature, int32, StationIndex, bool, bSucceeded, EWeaponLaunchFailureReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetDesignatedSignature, AActor*, TargetActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTargetClearedSignature);
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

	/** Event triggered when a launch attempt fails safety or prerequisite checks */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnWeaponLaunchFailedSignature OnWeaponLaunchFailed;

	/** Authoritative response to a client Fire request. */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnWeaponLaunchResultSignature OnWeaponLaunchResult;

	/** Event triggered when a new target is designated */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnTargetDesignatedSignature OnTargetDesignated;

	/** Event triggered when the designated target is cleared/deselected */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnTargetClearedSignature OnTargetCleared;

	/** Event triggered when a continuous gun begins firing */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnGunFiringStartedSignature OnGunFiringStarted;

	/** Event triggered when a continuous gun stops firing */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnGunFiringStoppedSignature OnGunFiringStopped;

	/** Event triggered when a swept-trace cannon bullet hits an object in the world */
	UPROPERTY(BlueprintAssignable, Category = "Mission Management|Events")
	FOnGunBulletHitSignature OnGunBulletHit;

	/** Array of hardpoint weapon stations configured on this aircraft */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Stations, Category = "Mission Management|Stations")
	TArray<FWeaponStation> Stations;

	/** Current Master Arm switch state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MasterArmMode, Category = "Mission Management|Control")
	EMasterArmMode MasterArmMode = EMasterArmMode::Safe;

	/** Current aircraft combat master mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MasterMode, Category = "Mission Management|Control")
	EAircraftMasterMode MasterMode = EAircraftMasterMode::AirToAir;

	/** If true, automatically spawns and attaches store visual actors to aircraft mesh sockets at BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission Management|Initialization")
	bool bAutoSpawnStoresOnBeginPlay = true;

	/** If true, automatically alternates to symmetrical or next valid station after firing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission Management|Control")
	bool bAutoStepStationOnFire = true;

	/** If true, weapon release is inhibited when the designated target is classified as Friendly via IGenericTeamAgentInterface */
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

	/** Assigns the designated sensor/radar target for weapon handoff */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Targeting")
	void SetDesignatedTarget(AActor* InTarget);
	/** Called by the radar after a server-validated linked designation. */
	void PrepareLinkedRadarWeapon(const FRadarTrack& LinkedTrack, UAircraftRadarComponent* SourceRadar);

	/** Returns the currently designated target actor (or nullptr if invalid/destroyed) */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Targeting")
	FORCEINLINE AActor* GetDesignatedTarget() const { return IsValid(DesignatedTarget) ? DesignatedTarget.Get() : nullptr; }

	/** Clears the designated target */
	UFUNCTION(BlueprintCallable, Category = "Mission Management|Targeting")
	void ClearDesignatedTarget();

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

	/** Calculates the Dynamic Launch Zone (DLZ: Rmin, Rne, Rmax) for the missile on the specified station against the designated radar target */
	UFUNCTION(BlueprintPure, Category = "Mission Management|Radar")
	bool CalculateMissileLaunchZone(int32 StationIndex, float& OutRmin, float& OutRne, float& OutRmax, bool& OutInShootingEnvelope) const;

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

	UFUNCTION(Server, Reliable, Category = "Mission Management|Targeting")
	void ServerSetDesignatedTarget(AActor* InTarget);

	UFUNCTION(Server, Reliable, Category = "Mission Management|Targeting")
	void ServerClearDesignatedTarget();

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

	/** Internal helper to release an individual store actor physically */
	AActor* ExecuteStoreRelease(FWeaponStation& Station, AActor* TargetActor);

	/** Internal callback fired when the designated target actor is destroyed */
	UFUNCTION()
	void HandleDesignatedTargetDestroyed(AActor* DestroyedActor);

	/** Internal method executed on each timer tick to step cannon firing cycles */
	void ProcessGunFireCycle(int32 StationIndex);

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

	/** Index of the currently selected station */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_SelectedStationIndex, Category = "Mission Management|Runtime")
	int32 SelectedStationIndex = 1;

	UFUNCTION()
	void OnRep_SelectedStationIndex();

	/** Target actor designated by aircraft radar/sensors */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, ReplicatedUsing = OnRep_DesignatedTarget, Category = "Mission Management|Runtime")
	TObjectPtr<AActor> DesignatedTarget = nullptr;

	UFUNCTION()
	void OnRep_DesignatedTarget();

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
	/** Finds index in Stations array for given station ID */
	int32 FindStationArrayIndex(int32 StationIndex) const;

};
