// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "MasterWeaponComponent.generated.h"
class APawn;
class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponFiredSignature, AActor*, TargetActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponDetachedSignature, AActor*, DetachedWeapon);

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

	/** Returns true if the weapon can be fired/launched */
	UFUNCTION(BlueprintPure, Category = "Weapon|Core")
	virtual bool CanFireWeapon() const;

	/** If true, the weapon must have a valid target lock before it can be fired. If false, it can be fired blindly (Maddog/Boresight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Core")
	bool bRequireLockToFire = true;

	/** Attempts to fire/launch the weapon */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Core")
	virtual bool FireWeapon();

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

	/** Enables debug visualization such as line traces and sensor cones */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Debug")
	bool bEnableDebugTraces = false;

protected:
	/** Owning/firing player aircraft pawn reference */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Weapon|Core")
	TObjectPtr<APawn> PlayerAircraft;

	/** Indicates whether the weapon systems are powered/activated */
	UPROPERTY(BlueprintReadOnly, Transient, Replicated, Category = "Weapon|Core")
	bool bIsWeaponActivated = false;

	/** Indicates whether the weapon has been fired */
	UPROPERTY(BlueprintReadOnly, Transient, Replicated, Category = "Weapon|Core")
	bool bWeaponFired = false;

	/** Elapsed time in seconds since the weapon was fired */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Weapon|Core")
	float TimeSinceFired = 0.0f;
};
