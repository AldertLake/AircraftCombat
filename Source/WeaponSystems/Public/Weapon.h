// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "Weapon.generated.h"

class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponMountedStateChangedSignature, bool, bIsMounted, AActor*, Carrier);

/**
 * Base Weapon Actor class for WeaponSystems.
 * Provides a static mesh body, collision lifecycle management (mounted vs. released),
 * and carrier aircraft mutual collision ignore logic to eliminate physics interference.
 */
UCLASS()
class WEAPONSYSTEMS_API AWeapon : public AActor
{
	GENERATED_BODY()

public:
	AWeapon();

	/** Returns the static mesh component representing the weapon body */
	FORCEINLINE UStaticMeshComponent* GetWeaponMesh() const { return WeaponMesh; }

	/** If true, collision is completely disabled while mounted to an aircraft to eliminate physics jitter/spinning */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Collision")
	bool bDisableCollisionWhileMounted = true;

	/** Collision enabled mode while mounted on a station/carrier (defaults to NoCollision) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Collision")
	TEnumAsByte<ECollisionEnabled::Type> MountedCollision = ECollisionEnabled::NoCollision;

	/** Collision enabled mode after release/firing from carrier (defaults to QueryAndPhysics) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Collision")
	TEnumAsByte<ECollisionEnabled::Type> ReleasedCollision = ECollisionEnabled::QueryAndPhysics;

	/** Optional collision profile name to apply upon release (leave None to preserve existing profile) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Collision")
	FName ReleasedCollisionProfile = NAME_None;

	/** If true, the released weapon permanently ignores collision with the launching carrier aircraft */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Collision")
	bool bPermanentlyIgnoreCarrier = true;

	/** If true, the released weapon ignores all other attached stores and pylons on the carrier */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Collision")
	bool bIgnoreSiblingStores = true;

	/** If true, simulate rigid body physics when released (used for unguided bombs/debris without guidance) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Collision")
	bool bSimulatePhysicsOnRelease = false;

	/** Brief safety arming window (seconds) after detachment before enabling collision if using delayed arming */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Collision", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SeparationSafetyDelay = 0.05f;

	/** Broadcast when mounting state changes */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponMountedStateChangedSignature OnWeaponMountedStateChanged;

	/** Sets whether the weapon is currently mounted to a carrier aircraft */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Mounting")
	virtual void SetMounted(bool bInMounted, AActor* InCarrier = nullptr);

	/** Returns true if the weapon is currently mounted to an aircraft hardpoint */
	UFUNCTION(BlueprintPure, Category = "Weapon|Mounting")
	FORCEINLINE bool IsMounted() const { return bIsMounted; }

	/** Returns the carrier aircraft this weapon is mounted to or was launched from */
	UFUNCTION(BlueprintPure, Category = "Weapon|Mounting")
	AActor* GetCarrierAircraft() const { return CarrierAircraft.Get(); }

	/** Explicitly sets the carrier aircraft reference */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Mounting")
	void SetCarrierAircraft(AActor* InCarrier);

	/** Applies mounted collision settings (disables collision and sets up carrier ignores) */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Collision")
	virtual void ApplyMountedCollisionState();

	/** Applies released collision settings (enables collision while strictly ignoring carrier) */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Collision")
	virtual void ApplyReleasedCollisionState();

	/** Sets up bidirectional collision ignore between this weapon and another actor across all components */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Collision")
	void SetupMutualCollisionIgnore(AActor* OtherActor, bool bShouldIgnore = true);

	/** Clears bidirectional collision ignore between this weapon and another actor */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Collision")
	void ClearMutualCollisionIgnore(AActor* OtherActor);

protected:
	virtual void BeginPlay() override;

	/** Root static mesh component representing the weapon body */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Mesh", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	/** Internal flag indicating if currently mounted */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_IsMounted, Category = "Weapon|Mounting")
	bool bIsMounted = false;

	/** Reference to the carrier aircraft */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_CarrierAircraft, Category = "Weapon|Mounting")
	TWeakObjectPtr<AActor> CarrierAircraft;

	UFUNCTION()
	virtual void OnRep_IsMounted();

	UFUNCTION()
	virtual void OnRep_CarrierAircraft();

	/** Timer handle for delayed separation arming */
	FTimerHandle SeparationSafetyTimerHandle;

	/** Blueprint hook called when mounted state changes */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|Events")
	void OnMountedStateChanged(bool bNewMounted, AActor* Carrier);
	virtual void OnMountedStateChanged_Implementation(bool bNewMounted, AActor* Carrier);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:	
	virtual void Tick(float DeltaTime) override;
};
