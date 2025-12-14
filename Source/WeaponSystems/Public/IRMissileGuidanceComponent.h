// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "MissileGuidanceComponent.h"
#include "IRMissileGuidanceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHeatSeekerChangeTargetSignature, AActor*, NewTarget, AActor*, OldTarget);

/**
 * Infrared (IR) Homing Missile Guidance Component
 *
 * Extends the shared missile guidance base with a full infrared heat-seeking head simulation.
 * Implements a 3-stage seeker acquisition pipeline (broad-phase sphere overlap -> narrow-phase cone angle -> LOS raycast),
 * gimbal-limited seeker cone tracking, and flare/distractor seduction logic.
 *
 * Use this component for heat-seeking missiles such as AIM-9 Sidewinder, IRIS-T, R-73, Python 5, etc.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UIRMissileGuidanceComponent : public UMissileGuidanceComponent
{
	GENERATED_BODY()

public:
	UIRMissileGuidanceComponent();
	virtual bool PrepareLaunch(const FMissileLaunchConfiguration& Configuration) override;
	virtual bool CanFireWeapon(EWeaponLaunchFailureReason& OutReason) const override;

	/** Event dispatcher broadcast when the seeker is distracted/seduced by another heat source (flares or crossing jet) and switches targets */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Events")
	FOnHeatSeekerChangeTargetSignature OnHeatSeekerChangeTarget;

	/** Maximum seeker detection and tracking distance in centimeters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Infrared Homing Head|Heat Seeking Head")
	float MaxSensorRange = 100000.0f;

	/** Field of View cone half-angle during search/acquisition mode in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Infrared Homing Head|Heat Seeking Head")
	float SearchModeConeAngle = 20.0f;

	/** Field of View cone half-angle during active locked tracking mode in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Infrared Homing Head|Heat Seeking Head")
	float LockedModeConeAngle = 3.5f;

	/** Minimum allowed seeker cone rotation angle in degrees (X = Min Pitch [Down], Y = Min Yaw [Left]) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Infrared Homing Head|Heat Seeking Head")
	FVector2D MinConeRotation = FVector2D(-45.0f, -45.0f);

	/** Maximum allowed seeker cone rotation angle in degrees (X = Max Pitch [Up], Y = Max Yaw [Right]) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Infrared Homing Head|Heat Seeking Head")
	FVector2D MaxConeRotation = FVector2D(45.0f, 45.0f);

	/** Probability (0.0 to 1.0) of seeker switching lock to a new nearby heat source (flares or crossing aircraft) entering the tracking cone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Infrared Homing Head|Countermeasures", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float TargetDistractionChance = 0.35f;

	/**
	 * Performs a 3-stage directional 3D cone check (Broad-phase sphere overlap -> Narrow-phase cone angle check -> Line-of-Sight raycast)
	 * using the seeker socket location and gimballed cone orientation to acquire and track heat-emitting targets.
	 *
	 * @param MaxRange Maximum detection range of the seeker in centimeters
	 * @param ConeHalfAngleDeg Half-angle of the seeker field of view cone in degrees (e.g. 2.5 deg caged, 45.0 deg uncaged)
	 * @param ConeRotation Seeker cone angular offset in degrees (X = Pitch [Up/Down], Y = Yaw [Right/Left])
	 * @param OverlapChannel Collision channel used for broad-phase candidate overlap detection (e.g. ECC_Pawn, ECC_WorldDynamic)
	 * @param OutTargets Array populated with unique, valid detected actors that have direct line of sight
	 * @return True if at least one valid target was acquired, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|Infrared Homing Head")
	bool FindTargetsInSeekerCone(
		float MaxRange,
		float ConeHalfAngleDeg,
		FVector2D ConeRotation,
		ECollisionChannel OverlapChannel,
		TArray<AActor*>& OutTargets
	);

	/**
	 * Slaves the IR seeker head to a designated target (e.g. from radar / SMS target designation).
	 * Enforces that the target must be within MaxSensorRange, line of sight must be clear, and within seeker gimbal limits.
	 * A valid cue aims the head for acquisition; it does not by itself grant a thermal lock.
	 * Out-of-range or obstructed cues are rejected without changing the current seeker aim.
	 *
	 * @param InTarget Target actor to slave seeker head towards
	 * @return True if the target was within range and the seeker accepted the cue
	 */
	virtual bool SlaveToTarget(AActor* InTarget) override;

	/**
	 * Slaves the IR seeker head to a specific world location (e.g. helmet look intersect, radar spotlight, or ground point).
	 * The seeker will point its acquisition cone towards this location (clamped to gimbal limits) and search for heat targets.
	 *
	 * @param InWorldLocation World space coordinate to slave the seeker head towards
	 * @return True if the target location is within the mechanical gimbal limits; false if clamped.
	 */
	virtual bool SlaveToLocation(const FVector& InWorldLocation) override;

	/**
	 * Slaves the IR seeker head along a specific world direction vector (e.g. pilot helmet look vector / HMD boresight).
	 *
	 * @param InWorldDirection Normalized world direction vector to aim the seeker towards
	 * @return True if direction is within mechanical gimbal limits; false if clamped.
	 */
	virtual bool SlaveToDirection(const FVector& InWorldDirection) override;

	/** Returns the seeker head to missile boresight (caged forward search) */
	virtual void SlaveToBoresight() override;
	virtual void SetSeekerCaged(bool bCaged) override;
	virtual bool IsSeekerCaged() const override { return bSeekerCaged; }
	virtual FVector GetSeekerLookDirection() const override;
	virtual FVector2D GetSeekerGimbalAngles() const override { return CurrentConeRotation; }
	virtual float GetSeekerGimbalLimitAngle() const override;
	virtual EWeaponAudioTone GetSeekerAudioTone() const override;
	virtual float GetSeekerSignalStrength() const override;
	virtual void LockMissile(AActor* InTargetActor) override;
	virtual void ActivateWeapon(bool bActivate = true) override;

	/** Returns true if an external acquisition cue is retained, including while tracking a thermal lock */
	UFUNCTION(BlueprintPure, Category = "Missile|Infrared Homing Head")
	FORCEINLINE bool IsSeekerSlavedToLocation() const { return bIsSlavedToLocation; }

	/** Returns the currently commanded slaved world location */
	UFUNCTION(BlueprintPure, Category = "Missile|Infrared Homing Head")
	FORCEINLINE FVector GetSlavedLocation() const { return SlavedWorldLocation; }


	/** Returns the target distraction probability (0.0 to 1.0) */
	UFUNCTION(BlueprintPure, Category = "Missile|Infrared Homing Head|Countermeasures")
	FORCEINLINE float GetTargetDistractionChance() const { return TargetDistractionChance; }

	/** Returns the minimum allowed seeker cone rotation limits */
	UFUNCTION(BlueprintPure, Category = "Missile|Infrared Homing Head|Heat Seeking Head")
	FORCEINLINE FVector2D GetMinConeRotation() const { return MinConeRotation; }

	/** Returns the maximum allowed seeker cone rotation limits */
	UFUNCTION(BlueprintPure, Category = "Missile|Infrared Homing Head|Heat Seeking Head")
	FORCEINLINE FVector2D GetMaxConeRotation() const { return MaxConeRotation; }

protected:
	/**
	 * IR-specific seeker state machine. Handles search/acquisition mode (wide cone)
	 * and locked tracking mode (narrow cone aimed at target heat source).
	 * Includes flare/distractor seduction logic.
	 */
	virtual void TickSeekerLogic(float DeltaTime) override;

private:
	/** Tracks actors present in the narrow seeker cone during the previous tick to detect newly entered distractors */
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<AActor>> PreviousNarrowConeTargets;

	/** Retained acquisition command used again if thermal tracking loses its target */
	UPROPERTY(Transient)
	bool bIsSlavedToLocation = false;

	/** True if the seeker head is slaved along a directional vector rather than a point in space */
	UPROPERTY(Transient)
	bool bIsSlavedToDirection = false;

	/** Commanded world location the seeker head is slaved towards */
	UPROPERTY(Transient)
	FVector SlavedWorldLocation = FVector::ZeroVector;

	/** Commanded world direction vector the seeker head is slaved along */
	UPROPERTY(Transient)
	FVector SlavedWorldDirection = FVector::ForwardVector;

	/** Current physical seeker gimbal angles in degrees (X = Pitch, Y = Yaw) */
	UPROPERTY(Transient)
	FVector2D CurrentConeRotation = FVector2D::ZeroVector;
	TWeakObjectPtr<AActor> SlavedTarget;
	bool bSeekerCaged = true;
};
