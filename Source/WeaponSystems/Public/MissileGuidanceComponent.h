// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "MasterWeaponComponent.h"
#include "Engine/EngineTypes.h"
#include "Net/UnrealNetwork.h"
#include "CombatTeamUtility.h"
#include "MissileGuidanceComponent.generated.h"

class AWeapon;
class APawn;
class AActor;
class UAircraftRadarComponent;

/** A measured track or an explicitly valid world-space waypoint. Zero is a valid position. */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FMissileTargetSolution
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Target")
	bool bValid = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Target")
	bool bMeasured = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Target")
	TObjectPtr<AActor> TargetActor = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Target")
	FVector Position = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Target")
	FVector Velocity = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Target")
	float MeasurementTimeSeconds = 0.0f;
	/** Receiver-assigned engagement identity. Source IDs below describe provenance only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Target")
	int32 TargetContactID = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Target")
	int32 SourceParticipantID = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Target")
	int32 SourceTrackID = INDEX_NONE;
};

/** Supplied by an aircraft SMS or by a standalone missile caller before launch. */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FMissileLaunchConfiguration
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Launch")
	TObjectPtr<APawn> Carrier = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Launch")
	FMissileTargetSolution Target;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Launch")
	TObjectPtr<UAircraftRadarComponent> Illuminator = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Launch")
	TObjectPtr<UAircraftRadarComponent> Uplink = nullptr;
	/** Radar that owns the launch record, even if another radar illuminates or donates tracks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Launch")
	TObjectPtr<UAircraftRadarComponent> LaunchRadar = nullptr;
	/** Track ID in the launching radar's files; -1 when no track was selected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Launch")
	int32 LaunchTrackID = -1;
	/** Autonomous seeker launch with no designated track or coordinate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Launch")
	bool bMadDog = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetLockedSignature, AActor*, LockedTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetLockLostSignature, AActor*, LostTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissileLaunchedSignature, AActor*, LockedTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProximityFuzeTriggeredSignature, AActor*, TriggeringActor);

/**
 * Defines the direction vector used for velocity inheritance during missile launch
 */
UENUM(BlueprintType)
enum class EVelocityInheritanceDirection : uint8
{
	MissileForward UMETA(DisplayName = "Missile Forward Vector"),
	AircraftForward UMETA(DisplayName = "Aircraft Forward Vector"),
	AircraftVelocityVector UMETA(DisplayName = "Aircraft Physical Velocity Vector")
};

/**
 * Shared base Missile Guidance Component handling universal missile flight kinematics,
 * True Proportional Navigation steering, rocket motor profile, proximity fuze,
 * and velocity inheritance. Subclasses implement seeker-specific logic (IR, Radar, etc.)
 * via the TickSeekerLogic() virtual hook.
 *
 * This class is NOT meant to be instantiated directly. Use UIRMissileGuidanceComponent
 * for infrared-homing missiles or URadarMissileGuidanceComponent for radar-guided missiles.
 */
UCLASS(Abstract, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UMissileGuidanceComponent : public UMasterWeaponComponent
{
	GENERATED_BODY()

public:
	UMissileGuidanceComponent();

	/** Event dispatcher broadcast when a target lock is successfully acquired */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Events")
	FOnTargetLockedSignature OnTargetLocked;

	/** Event dispatcher broadcast when a previously acquired target lock is lost or invalidated */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Events")
	FOnTargetLockLostSignature OnTargetLockLost;

	/** Event dispatcher broadcast when the proximity fuze detonates near a valid target */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Events")
	FOnProximityFuzeTriggeredSignature OnProximityFuzeTriggered;

	/** Time delay in seconds after launch before the rocket motor ignites */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Kinematics", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float MotorIgnitionDelay = 0.3f;

	/** Thrust acceleration rate in cm/s^2 while the rocket motor is burning */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Kinematics", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MotorAcceleration = 6000.0f;

	/** Optional boost, sustain, then unpowered coast. Existing missiles retain the legacy motor by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Motor Profile")
	bool bUseStagedMotorProfile = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Motor Profile", meta = (EditCondition = "bUseStagedMotorProfile", ClampMin = "0.0"))
	float BoostDurationSeconds = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Motor Profile", meta = (EditCondition = "bUseStagedMotorProfile", ClampMin = "0.0"))
	float SustainAcceleration = 1500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Motor Profile", meta = (EditCondition = "bUseStagedMotorProfile", ClampMin = "0.0"))
	float SustainDurationSeconds = 10.0f;
	/** Axial drag acceleration = coefficient * speed squared, in cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Motor Profile", meta = (EditCondition = "bUseStagedMotorProfile", ClampMin = "0.0"))
	float QuadraticDragCoefficient = 0.0000001f;

	/** Maximum cruise velocity of the missile in cm/s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Kinematics", meta = (ClampMin = "100.0", UIMin = "1000.0"))
	float MaxCruiseSpeed = 35000.0f;

	/** Time delay in seconds after launch before PN guidance steering begins (clears parent aircraft) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Kinematics", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float GuidanceActivationDelay = 0.5f;

	/** Proportional Navigation navigation constant / gain N (typically 3.0 to 5.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Kinematics", meta = (ClampMin = "1.0", ClampMax = "10.0", UIMin = "2.0", UIMax = "6.0"))
	float NavigationGain = 4.0f;

	/** Maximum lateral steering acceleration in Gs (1 G = 980.665 cm/s^2) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Kinematics", meta = (ClampMin = "1.0", ClampMax = "100.0", UIMin = "5.0", UIMax = "60.0"))
	float MaxLateralG = 25.0f;

	/** Maximum turning rate of the missile heading in degrees per second (smooths aerodynamic steering and prevents violent angular snapping) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Kinematics", meta = (ClampMin = "1.0", ClampMax = "720.0", UIMin = "10.0", UIMax = "180.0"))
	float MaxTurnRate = 60.0f;

	/** Distance cutoff in cm where steering freezes near impact to avoid mathematical singularities */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Kinematics", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1000.0"))
	float TerminalDeadbandRange = 100.0f;

	/** If true, the missile will inherit speed/velocity from the launching aircraft. If false, it starts purely with InitialSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Velocity Inheritance")
	bool bInheritAircraftVelocity = true;

	/** Determines which vector direction to apply the inherited speed along */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Velocity Inheritance", meta = (EditCondition = "bInheritAircraftVelocity"))
	EVelocityInheritanceDirection InheritanceDirection = EVelocityInheritanceDirection::MissileForward;

	/** Multiplier for the inherited speed (e.g. 1.0 = full speed, 0.5 = half speed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Velocity Inheritance", meta = (EditCondition = "bInheritAircraftVelocity", ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float InheritedSpeedMultiplier = 1.0f;

	/** If true, smoothly stabilizes missile attitude along the launch rail during MotorIgnitionDelay instead of snapping to composite velocity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Separation")
	bool bSmoothEjectionAttitude = true;

	/** Rate (deg/s) at which missile smoothly aligns with relative airflow during inert ejection separation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Separation", meta = (EditCondition = "bSmoothEjectionAttitude", ClampMin = "0.5", UIMin = "0.5"))
	float EjectionAlignmentRate = 4.0f;

	/** If true, proximity fuze automatically checks for nearby targets during active lock flight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Proximity Fuze")
	bool bEnableProximityFuze = true;

	/** Proximity fuze detonation radius in centimeters around the seeker socket */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Proximity Fuze", meta = (EditCondition = "bEnableProximityFuze", ClampMin = "10.0", UIMin = "50.0", UIMax = "5000.0"))
	float ProximityFuzeRadius = 500.0f;

	/** If true, the proximity fuze will only detonate on the currently locked target. If false, it detonates on any valid target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Proximity Fuze", meta = (EditCondition = "bEnableProximityFuze"))
	bool bFuzeOnlyTriggersOnLockedTarget = false;

	/** Delay in seconds after launch before proximity fuze arms (prevents detonation near launching aircraft or sibling weapons) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Proximity Fuze", meta = (EditCondition = "bEnableProximityFuze", ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float FuzeArmingDelay = 0.5f;

	/** Socket name located on THIS MISSILE mesh representing the seeker origin and forward orientation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Seeker")
	FName SeekerSocket = FName(TEXT("Seeker"));

	/** Socket name located on the TARGET mesh representing the primary tracking point (e.g. engine exhaust, radar reflector) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Seeker")
	FName TargetTrackingSocket = FName(TEXT("Engine"));

	/** Actor tags required for target acquisition by seeker (if empty, all overlapping targets are considered) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Seeker|Filter")
	TArray<FName> TargetFilterTags;

	/** If true, the firing player aircraft is excluded from seeker detection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Seeker|Filter")
	bool bIgnoreSelfAircraft = true;

	/** If true, attached child actors (e.g. wing pylons, fuel tanks, other mounted missiles) of the firing platform are also excluded */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Seeker|Filter", meta = (EditCondition = "bIgnoreSelfAircraft"))
	bool bIgnoreSelfAttachedActors = true;

	/** If true, queries all dynamic object types (WorldDynamic, Pawn, PhysicsBody, Vehicle) instead of just DetectionChannel */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Seeker|Filter")
	bool bQueryAllDynamicObjects = true;

	/** If true, the seeker uses IGenericTeamAgentInterface to exclude friendly targets from acquisition.
	 *  Disable for older/dumber missiles (e.g. AIM-9M) that track any heat source regardless of allegiance.
	 *  Enable for modern missiles (e.g. AIM-9X) with IFF interrogation capability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Seeker|IFF")
	bool bEnableIFF = false;

	/** How targets without IGenericTeamAgentInterface are treated when IFF is enabled (e.g. decoys, debris, unregistered actors) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Seeker|IFF", meta = (EditCondition = "bEnableIFF"))
	EIFFUnknownAttitude UnknownTargetAttitude = EIFFUnknownAttitude::Hostile;

	/** Collision channel used for seeker target detection overlaps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Guidance|Seeker|Filter")
	TEnumAsByte<ECollisionChannel> DetectionChannel = ECC_Pawn;

	/** Assigns an explicit list of actors to be ignored by seeker detection, tracking, and proximity fuze */
	UFUNCTION(BlueprintCallable, Category = "Missile|Guidance|Filter")
	void SetIgnoredActors(const TArray<AActor*>& InActors);

	/** Adds an actor to the seeker/fuze ignored list */
	UFUNCTION(BlueprintCallable, Category = "Missile|Guidance|Filter")
	void AddIgnoredActor(AActor* InActor);

	/** Clears the explicit ignored actors list */
	UFUNCTION(BlueprintCallable, Category = "Missile|Guidance|Filter")
	void ClearIgnoredActors();

	/** Returns currently configured ignored actors */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance|Filter")
	TArray<AActor*> GetIgnoredActors() const;

	/**
	 * Powers and activates the missile guidance/seeker electronics and enables target tracking while mounted.
	 *
	 * @param bActivate Powers and enables the missile guidance system if true, shuts it down if false
	 */
	virtual void ActivateWeapon(bool bActivate = true) override;

	/**
	 * Assigns the target actor to track.
	 *
	 * @param InTargetActor Target actor to acquire and lock onto
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|Guidance")
	virtual void LockMissile(AActor* InTargetActor);

	/** Configure a missile before launch; the caller may be an SMS or standalone actor. */
	UFUNCTION(BlueprintCallable, Category = "Missile|Launch")
	virtual bool PrepareLaunch(const FMissileLaunchConfiguration& Configuration);

	UFUNCTION(BlueprintPure, Category = "Missile|Guidance")
	FMissileTargetSolution GetTargetSolution() const { return TargetSolution; }
	UFUNCTION(BlueprintCallable, Category = "Missile|Guidance")
	void ClearTargetSolution();

	/**
	 * Returns true if the missile guidance system is activated and holding a valid target lock
	 */
	virtual bool CanFireWeapon() const override;

	/** Returns true if the missile can be safely detached/jettisoned (i.e. not yet launched) */
	virtual bool CanDetachWeapon() const override;

	/**
	 * Attempts to launch the missile.
	 * Requires the missile to be powered/activated and have a valid target lock (checked via CanFireWeapon).
	 *
	 * @return True if launch conditions were met and launch succeeded, false otherwise
	 */
	virtual bool FireWeapon() override;

	/**
	 * Updates missile longitudinal thrust acceleration and executes Proportional Navigation (PN) steering kinematics.
	 * If the missile has a valid target lock, commands proportional navigation acceleration towards the collision triangle.
	 * If the missile has no target lock, maintains current speed and flies forward.
	 *
	 * @param DeltaTime Frame delta time in seconds
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|Guidance")
	virtual void UpdateGuidanceVelocity(float DeltaTime);

	/**
	 * Performs a spherical proximity fuze trace around the seeker socket.
	 * If a valid target is within the fuze radius, triggers OnProximityFuzeTriggered.
	 *
	 * @return True if a valid target triggered the proximity fuze, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|Guidance")
	bool CheckProximityFuze();

	/** Returns the location of the tracking point on the given target (checks TargetTrackingSocket on target or falls back to actor location) */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance")
	FVector GetTargetTrackingLocation(const AActor* InTarget) const;

	/** Computes the current origin location and base orientation of the seeker head on this missile */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance")
	void GetSeekerTransform(FVector& OutLocation, FRotator& OutRotation) const;

	/** Returns the cached owner weapon actor */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance")
	FORCEINLINE AWeapon* GetWeapon() const { return Weapon; }

	/** Returns the target actor currently locked/tracked */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance")
	FORCEINLINE AActor* GetLockedTarget() const { return LockedTarget; }

	/** Returns true if the missile guidance system is activated and tracking a valid target */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance")
	FORCEINLINE bool IsMissileLocked() const { return bIsWeaponActivated && IsValid(LockedTarget); }

	/** Returns true if the proximity fuze has already detonated */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance")
	FORCEINLINE bool HasFuzeTriggered() const { return bFuzeTriggered; }

	/** Returns the maximum turning rate in degrees per second */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance|Kinematics")
	FORCEINLINE float GetMaxTurnRate() const { return MaxTurnRate; }

	/** Returns the target filter tags */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance|Seeker|Filter")
	FORCEINLINE TArray<FName> GetTargetFilterTags() const { return TargetFilterTags; }

	/** Returns true if the firing player aircraft is ignored during seeker detection */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance|Seeker|Filter")
	FORCEINLINE bool ShouldIgnoreSelfAircraft() const { return bIgnoreSelfAircraft; }

	/** Returns true if attached children of the firing aircraft are ignored during seeker detection */
	UFUNCTION(BlueprintPure, Category = "Missile|Guidance|Seeker|Filter")
	FORCEINLINE bool ShouldIgnoreSelfAttachedActors() const { return bIgnoreSelfAircraft && bIgnoreSelfAttachedActors; }

protected:
	virtual void BeginPlay() override;
	/** Position and velocity used by PN. Sensor-specific missiles may supply measured state. */
	virtual bool GetGuidanceTargetSolution(FMissileTargetSolution& OutSolution) const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Virtual hook for subclass-specific seeker logic. Called every tick when the weapon is activated.
	 * Override in UIRMissileGuidanceComponent for IR heat-seeking behavior.
	 * Override in URadarMissileGuidanceComponent for radar seeker behavior.
	 *
	 * @param DeltaTime Frame delta time in seconds
	 */
	virtual void TickSeekerLogic(float DeltaTime) {}

	/** Explicit list of actors to ignore during seeker queries and proximity fuze (e.g. parent aircraft & sibling stores) */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> AdditionalIgnoredActors;

	/** Cached owning weapon actor reference */
	UPROPERTY(BlueprintReadOnly, Category = "Missile|Guidance")
	TObjectPtr<AWeapon> Weapon;

	/** Target actor currently tracked and guided towards */
	UPROPERTY(Transient, Replicated)
	TObjectPtr<AActor> LockedTarget;

	UPROPERTY(Transient, Replicated)
	FMissileTargetSolution TargetSolution;

	/** Indicates whether the proximity fuze has detonated (prevents repeated trigger callbacks) */
	UPROPERTY(Transient)
	bool bFuzeTriggered = false;

	/** Populates query parameters with ignored actors (owner, missile, player aircraft, and attached children) */
	void PopulateSeekerIgnoredActors(FCollisionQueryParams& OutParams) const;

	/** Evaluates whether a candidate actor passes basic filtering (validity, friendly exclusion, and target tags) */
	bool IsCandidateTargetEligible(const AActor* Candidate) const;
	virtual bool IsFuzeTargetEligible(const AActor* Candidate) const { return IsCandidateTargetEligible(Candidate); }

	/** Calculates the normalized rotated forward vector of the seeker head based on 2D pitch/yaw input */
	FVector ComputeRotatedSeekerForward(const FRotator& SeekerRotation, const FVector2D& ConeRotation) const;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
