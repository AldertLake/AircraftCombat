// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "AircraftCombatCommonTypes.h"
#include "MasterWeaponComponent.generated.h"
class APawn;
class AActor;
class UMasterWeaponComponent;
class UModularMissionManagement;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponFiredSignature, AActor*, TargetActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponDetachedSignature, AActor*, DetachedWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponFireResultSignature, bool, bSucceeded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWeaponSeekerStateChangedSignature, UMasterWeaponComponent*, Weapon, EWeaponSeekerState, OldState, EWeaponSeekerState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponLockAcquiredSignature, UMasterWeaponComponent*, Weapon, AActor*, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponLockLostSignature, UMasterWeaponComponent*, Weapon, AActor*, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponCageStateChangedSignature, UMasterWeaponComponent*, Weapon, bool, bCaged);

/** Sensor cue needed for a targeted launch. Seeker-equipped weapons may still launch without a cue. */
UENUM(BlueprintType)
enum class EWeaponFiringRequirement : uint8
{
	Nothing,
	Bugging,
	HardLock
};

/**
 * Abstract base class for weapon components handling common functionality
 */
UCLASS(Abstract, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UMasterWeaponComponent : public UProjectileMovementComponent
{
	GENERATED_BODY()

public:
	UMasterWeaponComponent();

	/** Event dispatcher broadcast when the weapon is successfully fired. TargetActor may be null if the weapon does not support target locking. */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponFiredSignature OnWeaponFired;

	/** Event dispatcher broadcast when the weapon is detached from the aircraft */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponDetachedSignature OnWeaponDetached;

	/** Authoritative result of a client RequestFireWeapon call. */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponFireResultSignature OnWeaponFireResult;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponSeekerStateChangedSignature OnSeekerStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponLockAcquiredSignature OnLockAcquired;
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponLockLostSignature OnLockLost;
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponCageStateChangedSignature OnCageStateChanged;

	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	EWeaponComponentType GetWeaponComponentType() const { return WeaponComponentType; }
	UFUNCTION(BlueprintCallable, Category = "Weapon|Guidance")
	virtual bool SlaveToDirection(const FVector& InWorldDirection);
	UFUNCTION(BlueprintCallable, Category = "Weapon|Guidance")
	virtual bool SlaveToLocation(const FVector& InWorldLocation);
	UFUNCTION(BlueprintCallable, Category = "Weapon|Guidance")
	virtual bool SlaveToTarget(AActor* InTarget);
	UFUNCTION(BlueprintCallable, Category = "Weapon|Guidance")
	virtual void SlaveToBoresight();
	UFUNCTION(BlueprintCallable, Category = "Weapon|Guidance")
	virtual void SetSeekerCaged(bool bCaged);
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual bool IsSeekerCaged() const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual EWeaponSeekerState GetSeekerState() const { return SeekerState; }
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual FVector GetSeekerLookDirection() const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual FVector2D GetSeekerGimbalAngles() const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual float GetSeekerGimbalLimitAngle() const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual EWeaponAudioTone GetSeekerAudioTone() const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual float GetSeekerSignalStrength() const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual bool GetDynamicLaunchZone(const AActor* Target, float& OutRmin, float& OutRne, float& OutRmax) const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual bool IsTargetInLaunchEnvelope(const AActor* Target) const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual float GetEstimatedTimeToImpact(const AActor* Target) const;
	UFUNCTION(BlueprintPure, Category = "Weapon|Guidance")
	virtual float GetEstimatedTimeToActive(const AActor* Target) const;

	/** Initializes the weapon component with the owning player aircraft */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Core")
	virtual void InitializeWeapon(APawn* InPlayerAircraft);

	/**
	 * Powers and activates the weapon system.
	 *
	 * @param bActivate Powers and enables the weapon if true, shuts it down if false
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Core")
	virtual void ActivateWeapon(bool bActivate = true);

	/**
	 * Checks if the weapon is ready and permitted to fire/launch.
	 *
	 * @param OutReason Diagnostic launch failure reason if weapon cannot fire
	 * @return True if weapon can fire
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Core")
	virtual bool CanFireWeapon(EWeaponLaunchFailureReason& OutReason) const;

	/** C++ convenience overload for checking weapon firing readiness without diagnostic output */
	FORCEINLINE bool CanFireWeapon() const
	{
		EWeaponLaunchFailureReason UnusedReason = EWeaponLaunchFailureReason::None;
		return CanFireWeapon(UnusedReason);
	}

	/** Minimum sensor state for a targeted launch. Autonomous seekers can search after an uncued launch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Core")
	EWeaponFiringRequirement FiringRequirement = EWeaponFiringRequirement::Nothing;

	/** Attempts to fire/launch the weapon */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Core")
	virtual bool FireWeapon();

	/** Server callers receive the launch result; owning clients submit a server request. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Core")
	bool RequestFireWeapon();

	/** Applies an ejection impulse to the weapon upon launch/drop from a pylon */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Core")
	virtual void ApplyEjectionImpulse(const FVector& Impulse);

	/** Returns the ejection impulse applied to this weapon */
	UFUNCTION(BlueprintPure, Category = "Weapon|Launch")
	FORCEINLINE FVector GetAppliedEjectionImpulse() const { return AppliedEjectionImpulse; }

	/** Returns the world rotation of the weapon at the moment of launch/detachment */
	UFUNCTION(BlueprintPure, Category = "Weapon|Launch")
	FORCEINLINE FRotator GetLaunchRotation() const { return LaunchRotation; }

	/** Returns true if the weapon can be safely detached/jettisoned */
	UFUNCTION(BlueprintPure, Category = "Weapon|Core")
	virtual bool CanDetachWeapon() const;

	/** Detaches the weapon from the aircraft (jettison/drop) */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Core")
	virtual void DetachWeapon();

	/** Returns the player aircraft pawn reference */
	UFUNCTION(BlueprintPure, Category = "Weapon|Core")
	FORCEINLINE APawn* GetPlayerAircraft() const { return PlayerAircraft; }

	/** Returns true if the weapon system is powered/activated */
	UFUNCTION(BlueprintPure, Category = "Weapon|Core")
	FORCEINLINE bool IsWeaponActivated() const { return bIsWeaponActivated; }

	/** Returns true if the weapon has been fired/launched */
	UFUNCTION(BlueprintPure, Category = "Weapon|Core")
	FORCEINLINE bool IsWeaponFired() const { return bWeaponFired; }

	/** Returns the elapsed time in seconds since the weapon was fired */
	UFUNCTION(BlueprintPure, Category = "Weapon|Core")
	FORCEINLINE float GetTimeSinceFired() const { return TimeSinceFired; }

protected:
	/** Mounted weapons may only use FireWeapon while their station is executing a checked release. */
	bool IsDirectFirePermitted() const;
	UModularMissionManagement* GetMountedMission() const;
	friend class UModularMissionManagement;
	bool bMissionReleaseAuthorized = false;

	EWeaponComponentType WeaponComponentType = EWeaponComponentType::Unknown;
	UPROPERTY(Transient, BlueprintReadOnly, ReplicatedUsing = OnRep_SeekerState, Category = "Weapon|Guidance")
	EWeaponSeekerState SeekerState = EWeaponSeekerState::Standby;
	/** The single transition point for universal seeker and lock events. */
	void TransitionSeekerState(EWeaponSeekerState NewState, AActor* TrackedTarget = nullptr);
	UPROPERTY(Transient, ReplicatedUsing = OnRep_ReportedLockTarget)
	TObjectPtr<AActor> ReportedLockTarget = nullptr;
	TWeakObjectPtr<AActor> LastObservedLockTarget;
	UFUNCTION() void OnRep_SeekerState(EWeaponSeekerState OldState);
	UFUNCTION() void OnRep_ReportedLockTarget();
	/** Recorded ejection impulse applied during release */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Launch")
	FVector AppliedEjectionImpulse = FVector::ZeroVector;

	/** Recorded world orientation at the moment of release */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Weapon|Launch")
	FRotator LaunchRotation = FRotator::ZeroRotator;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server RPC to trigger weapon firing across network */
	UFUNCTION(Server, Reliable, Category = "Weapon|Core")
	void ServerFireWeapon();

	UFUNCTION(Client, Reliable, Category = "Weapon|Core")
	void ClientFireWeaponResult(bool bSucceeded);

	/** Enables debug visualization such as line traces and sensor cones */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Debug")
	bool bEnableDebugTraces = false;

protected:
	/** Owning/firing player aircraft pawn reference */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Weapon|Core")
	TObjectPtr<APawn> PlayerAircraft;

	/** Indicates whether the weapon systems are powered/activated */
	UPROPERTY(BlueprintReadOnly, Transient, ReplicatedUsing = OnRep_IsWeaponActivated, Category = "Weapon|Core")
	bool bIsWeaponActivated = false;

	/** Indicates whether the weapon has been fired */
	UPROPERTY(BlueprintReadOnly, Transient, ReplicatedUsing = OnRep_WeaponFired, Category = "Weapon|Core")
	bool bWeaponFired = false;

	/** Elapsed time in seconds since the weapon was fired */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Weapon|Core")
	float TimeSinceFired = 0.0f;

	UFUNCTION()
	virtual void OnRep_WeaponFired();

	UFUNCTION()
	virtual void OnRep_IsWeaponActivated();
};
