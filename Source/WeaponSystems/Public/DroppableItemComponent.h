// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "MasterWeaponComponent.h"
#include "DroppableItemComponent.generated.h"

class APawn;
class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDroppableStoreBouncedSignature, const FHitResult&, ImpactResult, const FVector&, ImpactVelocity);

/**
 * Specialized weapon/store component for droppable items (such as external fuel drop tanks,
 * unguided bombs, or jettisonable equipment pods).
 *
 * Utilizes the underlying UProjectileMovementComponent for ballistic gravity (100% / 1.0),
 * ground/water bouncing with configurable restitution/friction, and smooth separation kinematics.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UDroppableItemComponent : public UMasterWeaponComponent
{
	GENERATED_BODY()

public:
	UDroppableItemComponent();

	/** Broadcast when the droppable store impacts and bounces off a surface */
	UPROPERTY(BlueprintAssignable, Category = "Droppable|Events")
	FOnDroppableStoreBouncedSignature OnStoreBounced;

	/** If true, inherits linear velocity from the carrier aircraft upon detachment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Droppable|Velocity")
	bool bInheritAircraftVelocity = true;

	/** Multiplier applied to the carrier aircraft's inherited velocity (1.0 = 100% velocity) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Droppable|Velocity", meta = (EditCondition = "bInheritAircraftVelocity", ClampMin = "0.0", UIMin = "0.0"))
	float InheritedSpeedMultiplier = 1.0f;

	/** If true, smoothly blends store attitude from mount orientation into flight trajectory arc upon release */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Droppable|Separation")
	bool bSmoothSeparationAttitude = true;

	/** Rate (deg/s) at which store orientation aligns with velocity vector during initial separation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Droppable|Separation", meta = (EditCondition = "bSmoothSeparationAttitude", ClampMin = "0.5", UIMin = "0.5"))
	float SeparationAlignmentRate = 4.0f;

	/** If true, the droppable store will bounce off surfaces upon impact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Droppable|Bounce")
	bool bEnableBounce = true;

	/** Percentage of velocity retained after a bounce (0.0 = no bounce, 1.0 = fully elastic) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Droppable|Bounce", meta = (EditCondition = "bEnableBounce", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StoreBounciness = 0.35f;

	/** Friction coefficient applied during bounces and sliding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Droppable|Bounce", meta = (EditCondition = "bEnableBounce", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StoreFriction = 0.45f;

	/** Velocity threshold (cm/s) below which the store stops simulating and bouncing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Droppable|Bounce", meta = (EditCondition = "bEnableBounce", ClampMin = "0.0", UIMin = "0.0"))
	float BounceStopVelocityThreshold = 15.0f;

	/** If true, bounce angle affects friction (grazing impacts retain more momentum) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Droppable|Bounce", meta = (EditCondition = "bEnableBounce"))
	bool bStoreBounceAngleAffectsFriction = true;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Returns true if the droppable store can be released/fired */
	virtual bool CanFireWeapon() const override;

	/** Drops/releases the store from the aircraft */
	virtual bool FireWeapon() override;

	/** Returns true if the store can be safely detached/jettisoned */
	virtual bool CanDetachWeapon() const override;

	/**
	 * Detaches the store from the carrier aircraft.
	 * Activates projectile movement simulation, applies 100% gravity (1.0),
	 * configures bounce parameters, and inherits the aircraft's point velocity.
	 */
	virtual void DetachWeapon() override;

	/** Returns true if the store has already been detached from its mount */
	UFUNCTION(BlueprintPure, Category = "Droppable|Lifecycle")
	FORCEINLINE bool IsDetached() const { return bIsDetached; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** Indicates whether this store has been detached from its mount */
	UPROPERTY(BlueprintReadOnly, Transient, ReplicatedUsing = OnRep_IsDetached, Category = "Droppable|Lifecycle")
	bool bIsDetached = false;

	UFUNCTION()
	virtual void OnRep_IsDetached();

	/** Resolves the carrier aircraft reference if PlayerAircraft was not explicitly initialized */
	virtual APawn* ResolveAircraft() const;

	/** Internal handler bound to UProjectileMovementComponent::OnProjectileBounce */
	UFUNCTION()
	void HandleProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);
};
