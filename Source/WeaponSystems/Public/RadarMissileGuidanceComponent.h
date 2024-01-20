// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "MissileGuidanceComponent.h"
#include "RadarMissileGuidanceComponent.generated.h"

class UAircraftRadarComponent;

/**
 * Radar-guided missile guidance mode
 */
UENUM(BlueprintType)
enum class ERadarMissileGuidanceMode : uint8
{
	/** AIM-120 AMRAAM: Mid-course INS/datalink followed by autonomous onboard active radar terminal seeker */
	ActiveRadarHoming UMETA(DisplayName = "Active Radar Homing (ARH)"),

	/** AIM-7 Sparrow: Requires continuous illumination from the parent aircraft's radar in STT mode */
	SemiActiveRadarHoming UMETA(DisplayName = "Semi-Active Radar Homing (SARH)"),

	/** AGM-88 HARM: Homes on enemy radar emissions (no own radar, tracks emitter source) */
	PassiveRadarHoming UMETA(DisplayName = "Passive Radar Homing (Home-on-Jam / Anti-Radiation)"),

	/** Harpoon/SLAM: INS cruise to waypoint, then terminal active radar pop-up or sea-skim acquisition */
	InertialWithTerminal UMETA(DisplayName = "Inertial + Terminal Active (INS/ARH)")
};

/**
 * Current phase of radar missile flight
 */
UENUM(BlueprintType)
enum class ERadarMissileFlightPhase : uint8
{
	/** Pre-launch: Mounted on aircraft, receiving power and target data */
	PreLaunch UMETA(DisplayName = "Pre-Launch"),

	/** Mid-course: Flying toward predicted intercept point using INS/datalink updates */
	MidCourse UMETA(DisplayName = "Mid-Course (INS/Datalink)"),

	/** Loft: Pitching up for energy advantage at long range before diving terminal */
	Loft UMETA(DisplayName = "Loft Maneuver"),

	/** Terminal: Onboard seeker is active and autonomously tracking target (pitbull for ARH) */
	Terminal UMETA(DisplayName = "Terminal (Seeker Active)"),

	/** Sea-skim: Low-altitude cruise phase for anti-ship missiles */
	SeaSkim UMETA(DisplayName = "Sea-Skim Terminal"),

	/** Autonomous: Lost datalink, flying to last known position */
	Autonomous UMETA(DisplayName = "Autonomous (Lost Datalink)")
};

// Delegate declarations
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMidCourseUpdateReceivedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnActiveSeekerActivatedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRadarMissileLockLostSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTargetNotchingSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSeaSkimActivatedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDataLinkTimeoutSignature, float, TimeSinceLastUpdate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChaffDeployedNearbySignature, float, BreakLockProbability);

/**
 * Radar-Guided Missile Guidance Component
 *
 * Extends the shared missile guidance base with realistic radar missile behaviors including:
 * - Active Radar Homing (ARH): AIM-120 AMRAAM style — mid-course datalink + terminal onboard seeker
 * - Semi-Active Radar Homing (SARH): AIM-7 Sparrow style — requires parent radar illumination
 * - Passive Radar Homing: AGM-88 HARM style — homes on enemy radar emissions
 * - Inertial + Terminal: Harpoon style — INS waypoint cruise + terminal active seeker pop-up
 *
 * Features loft trajectory, Doppler notch filtering, chaff countermeasure susceptibility,
 * datalink updates from parent radar, and configurable terminal seeker parameters.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API URadarMissileGuidanceComponent : public UMissileGuidanceComponent
{
	GENERATED_BODY()

public:
	URadarMissileGuidanceComponent();

	/** Fired when a mid-course datalink update is received from the parent radar */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Radar|Events")
	FOnMidCourseUpdateReceivedSignature OnMidCourseUpdateReceived;

	/** Fired when the onboard active seeker activates (pitbull / terminal acquisition) */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Radar|Events")
	FOnActiveSeekerActivatedSignature OnActiveSeekerActivated;

	/** Fired when the radar missile loses its lock (SARH illumination lost or seeker break-lock) */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Radar|Events")
	FOnRadarMissileLockLostSignature OnRadarLockLost;

	/** Fired when the target drops below Doppler notch velocity threshold */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Radar|Events")
	FOnTargetNotchingSignature OnTargetNotching;

	/** Fired when the sea-skim terminal phase activates */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Radar|Events")
	FOnSeaSkimActivatedSignature OnSeaSkimActivated;

	/** Fired when datalink connection is lost (timeout, no updates from parent) */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Radar|Events")
	FOnDataLinkTimeoutSignature OnDataLinkTimeout;

	/** Fired when chaff is deployed nearby and may break the seeker lock */
	UPROPERTY(BlueprintAssignable, Category = "Missile|Radar|Events")
	FOnChaffDeployedNearbySignature OnChaffDeployedNearby;

	/** Primary guidance mode determining flight behavior and seeker type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Configuration")
	ERadarMissileGuidanceMode GuidanceMode = ERadarMissileGuidanceMode::ActiveRadarHoming;

	/** Range in cm at which the onboard active seeker activates for autonomous terminal guidance (pitbull range).
	 *  For AIM-120C: ~16km = 1,600,000 cm. Set to 0 to require manual activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Active Seeker", meta = (EditCondition = "GuidanceMode == ERadarMissileGuidanceMode::ActiveRadarHoming || GuidanceMode == ERadarMissileGuidanceMode::InertialWithTerminal"))
	float ActiveSeekerRange = 1600000.0f;

	/** Onboard radar seeker field-of-view half-angle in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Active Seeker", meta = (EditCondition = "GuidanceMode == ERadarMissileGuidanceMode::ActiveRadarHoming || GuidanceMode == ERadarMissileGuidanceMode::InertialWithTerminal"))
	float ActiveSeekerConeAngle = 30.0f;

	/** Maximum detection range of the onboard active seeker in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Active Seeker", meta = (EditCondition = "GuidanceMode == ERadarMissileGuidanceMode::ActiveRadarHoming || GuidanceMode == ERadarMissileGuidanceMode::InertialWithTerminal"))
	float ActiveSeekerMaxRange = 2000000.0f;

	/** How often in seconds the missile requests/receives datalink updates from the parent radar */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Datalink")
	float MidCourseUpdateInterval = 1.0f;

	/** Maximum range in cm for datalink communication with the parent aircraft */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Datalink")
	float DataLinkRange = 10000000.0f;

	/** If datalink is lost for this many multiples of MidCourseUpdateInterval, missile goes autonomous */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Datalink", meta = (ClampMin = "1.0"))
	float DataLinkTimeoutMultiplier = 3.0f;

	/** If true, this missile requires the parent aircraft's radar to maintain STT lock (illumination) for guidance.
	 *  Required for SARH missiles like AIM-7 Sparrow. If the parent breaks STT, the missile loses guidance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|SARH", meta = (EditCondition = "GuidanceMode == ERadarMissileGuidanceMode::SemiActiveRadarHoming"))
	bool bRequiresParentIllumination = true;

	/** If true, missile executes a loft maneuver at long range for energy advantage before terminal dive */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Loft")
	bool bEnableLoft = true;

	/** Pitch-up angle in degrees during loft maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Loft", meta = (EditCondition = "bEnableLoft", ClampMin = "5.0", ClampMax = "60.0"))
	float LoftAngle = 25.0f;

	/** Range to target in cm at which loft maneuver begins. Below this range, missile flies direct. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Loft", meta = (EditCondition = "bEnableLoft"))
	float LoftActivationRange = 4000000.0f;

	/** Range to target in cm at which loft ends and missile dives toward target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Loft", meta = (EditCondition = "bEnableLoft"))
	float LoftTerminationRange = 2000000.0f;

	/** If true, missile descends to sea-skim altitude for terminal phase (anti-ship missiles) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Sea-Skim", meta = (EditCondition = "GuidanceMode == ERadarMissileGuidanceMode::InertialWithTerminal"))
	bool bEnableSeaSkimming = false;

	/** Terminal sea-skim altitude above sea level in cm (e.g. 500 cm = 5 meters) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Sea-Skim", meta = (EditCondition = "bEnableSeaSkimming", ClampMin = "100.0"))
	float SeaSkimAltitude = 500.0f;

	/** Minimum target closure velocity in cm/s to maintain radar track. Targets flying perpendicular (notching) below this value will cause track loss.
	 *  Set to 0 to disable notch filtering. Realistic values: ~3000-5000 cm/s (30-50 m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Countermeasures")
	float NotchFilterVelocity = 3000.0f;

	/** Probability (0.0-1.0) that chaff deployed within the seeker FOV will break the missile's lock */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Countermeasures", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChaffBreakLockChance = 0.25f;

	/** Actor tags that identify chaff/decoy actors in the world */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Countermeasures")
	TArray<FName> ChaffDecoyTags;

	/** If true, missile can switch to Home-on-Jam mode when detecting ECM jamming */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|Radar|Countermeasures")
	bool bCanHomeOnJam = false;

	/**
	 * Provides a mid-course datalink update with the latest target position and velocity from the parent radar.
	 *
	 * @param TargetPos Latest known target world position
	 * @param TargetVel Latest known target velocity vector
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|Radar|Datalink")
	void ReceiveMidCourseUpdate(FVector TargetPos, FVector TargetVel);

	/**
	 * Sets the inertial navigation target location for INS cruise phase.
	 *
	 * @param InTargetLocation Target world location for INS guidance
	 * @param InTargetVelocity Predicted target velocity for dead-reckoning extrapolation
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|Radar|Guidance")
	void SetInertialTarget(FVector InTargetLocation, FVector InTargetVelocity);

	/**
	 * Sets the reference to the parent aircraft's radar component for SARH illumination checks and datalink.
	 *
	 * @param InRadar Pointer to the launching aircraft's radar component
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|Radar|Guidance")
	void SetParentRadar(UAircraftRadarComponent* InRadar);

	/** Returns true if the onboard active seeker is currently active (terminal/pitbull phase) */
	UFUNCTION(BlueprintPure, Category = "Missile|Radar|Guidance")
	FORCEINLINE bool IsInTerminalPhase() const { return FlightPhase == ERadarMissileFlightPhase::Terminal || FlightPhase == ERadarMissileFlightPhase::SeaSkim; }

	/** Returns the current flight phase */
	UFUNCTION(BlueprintPure, Category = "Missile|Radar|Guidance")
	FORCEINLINE ERadarMissileFlightPhase GetFlightPhase() const { return FlightPhase; }

	/** Returns the guidance mode */
	UFUNCTION(BlueprintPure, Category = "Missile|Radar|Configuration")
	FORCEINLINE ERadarMissileGuidanceMode GetRadarGuidanceMode() const { return GuidanceMode; }

	/** Returns the last known inertial target position */
	UFUNCTION(BlueprintPure, Category = "Missile|Radar|Guidance")
	FORCEINLINE FVector GetInertialTargetLocation() const { return InertialTargetLocation; }

	/** Returns the last known inertial target velocity */
	UFUNCTION(BlueprintPure, Category = "Missile|Radar|Guidance")
	FORCEINLINE FVector GetInertialTargetVelocity() const { return InertialTargetVelocity; }

	/** Returns the time since the last datalink update was received */
	UFUNCTION(BlueprintPure, Category = "Missile|Radar|Datalink")
	FORCEINLINE float GetTimeSinceLastDataLink() const { return TimeSinceLastDataLink; }

	/** Returns true if datalink communication is still active */
	UFUNCTION(BlueprintPure, Category = "Missile|Radar|Datalink")
	bool IsDataLinkActive() const;

	/** Returns the parent radar component (may be null if not set or parent destroyed) */
	UFUNCTION(BlueprintPure, Category = "Missile|Radar|Guidance")
	UAircraftRadarComponent* GetParentRadar() const;

	/** Override CanFireWeapon — ARH missiles can fire with inertial target data even without active lock */
	virtual bool CanFireWeapon() const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickSeekerLogic(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** Current flight phase of the radar missile */
	UPROPERTY(Transient, Replicated)
	ERadarMissileFlightPhase FlightPhase = ERadarMissileFlightPhase::PreLaunch;

	/** Last known target position from datalink or seeker measurement */
	UPROPERTY(Transient, Replicated)
	FVector InertialTargetLocation = FVector::ZeroVector;

	/** Last known target velocity for dead-reckoning extrapolation */
	UPROPERTY(Transient)
	FVector InertialTargetVelocity = FVector::ZeroVector;

	/** Time elapsed since the last datalink update was received */
	UPROPERTY(Transient)
	float TimeSinceLastDataLink = 0.0f;

	/** Cached reference to the parent aircraft's radar component */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAircraftRadarComponent> ParentRadarComponent;

	/** Whether the loft maneuver has been executed this flight */
	UPROPERTY(Transient)
	bool bLoftComplete = false;

	/** Whether the datalink timeout has been triggered */
	UPROPERTY(Transient)
	bool bDataLinkTimedOut = false;

	/** Whether the active seeker activation has been broadcast */
	UPROPERTY(Transient)
	bool bActiveSeekerBroadcast = false;

	// Internal seeker logic helpers
	void TickARHGuidance(float DeltaTime);
	void TickSARHGuidance(float DeltaTime);
	void TickPassiveGuidance(float DeltaTime);
	void TickInertialTerminalGuidance(float DeltaTime);
	void PerformActiveSeekerScan();
	void CheckDopplerNotch();
	void CheckChaffCountermeasures();
	void UpdateInertialDeadReckoning(float DeltaTime);
	bool CheckParentRadarIllumination() const;
	void TransitionToPhase(ERadarMissileFlightPhase NewPhase);
	FVector ComputeLoftDirection(const FVector& ForwardDir, const FVector& ToTarget) const;
};
