// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
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
	 * If target is out of sensor range, slaving is rejected and any active lock on this target is cleared.
	 *
	 * @param InTarget Target actor to slave seeker head towards
	 * @return True if target was within range and successfully slaved/locked
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|Infrared Homing Head")
	bool SlaveToDesignatedTarget(AActor* InTarget);

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
};
