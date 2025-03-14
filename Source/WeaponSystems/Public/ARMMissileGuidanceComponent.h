// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "MissileGuidanceComponent.h"
#include "AircraftRadarComponent.h"
#include "RadarWarningReceiverComponent.h"
#include "ARMMissileGuidanceComponent.generated.h"

class UAircraftRadarComponent;
class URadarWarningReceiverComponent;

/**
 * Operating mode for Anti-Radiation / SEAD missiles (AGM-88 HARM, ALARM, Kh-31P)
 */
UENUM(BlueprintType)
enum class EARMGuidanceMode : uint8
{
	/** Direct attack / boresight: Passively scans forward for active emitter radar signals within seeker FOV */
	TargetOfOpportunity UMETA(DisplayName = "Target of Opportunity (TOO / Direct Attack)"),

	/** Pre-briefed: Launched toward known GPS/world coordinates, activating passive seeker acquisition on approach */
	PreBriefed UMETA(DisplayName = "Pre-Briefed (PB / Known Position)"),

	/** Self-protect: Handoff directly from aircraft RWR when illuminated or locked by enemy radar */
	SelfProtect UMETA(DisplayName = "Self-Protect (SP / RWR Handoff)")
};

/**
 * Flight phase of an Anti-Radiation Missile
 */
UENUM(BlueprintType)
enum class EARMFlightPhase : uint8
{
	/** Mounted on parent aircraft, passively listening or waiting for target handoff */
	PreLaunch UMETA(DisplayName = "Pre-Launch"),

	/** Cruising toward target / pre-briefed area */
	MidCourse UMETA(DisplayName = "Mid-Course"),

	/** Pitching up for long-range energy retention before diving onto emitter */
	Loft UMETA(DisplayName = "Loft Maneuver"),

	/** Actively tracking live RF transmissions from target emitter */
	TerminalTracking UMETA(DisplayName = "Terminal Tracking (Live RF)"),

	/** Emitter shut down (EMCON defense) — navigating via GPS/INS coordinate memory */
	DeadReckoning UMETA(DisplayName = "Dead Reckoning (GPS/INS Memory)"),

	/** Emitter remained silent beyond memory drift timeout */
	MemoryTimeout UMETA(DisplayName = "Memory Timeout")
};

// Event Dispatchers
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnARMPhaseChangedSignature, EARMFlightPhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEmitterAcquiredSignature, AActor*, EmitterActor, UAircraftRadarComponent*, RadarComponent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEmitterLostSignature, AActor*, EmitterActor, FVector, LastKnownLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEmitterReacquiredSignature, AActor*, EmitterActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeadReckoningTimeoutSignature, FVector, FinalMemoryLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRWRHandoffReceivedSignature, int32, ThreatID, AActor*, ThreatActor);

/**
 * Anti-Radiation Missile (ARM) Guidance Component
 *
 * Dedicated guidance system for Suppression of Enemy Air Defenses (SEAD) and
 * Destruction of Enemy Air Defenses (DEAD) missiles (e.g. AGM-88 HARM, ALARM, Kh-31P).
 *
 * Key Capabilities:
 * - Passive Wideband RF Seeker: Detects and homes on electromagnetic emissions from UAircraftRadarComponent instances.
 * - Anti-Shutdown Defense (GPS/INS Memory): If the targeted radar shuts down transmitter power (EMCON defense),
 *   the missile transitions to Dead Reckoning, extrapolating the emitter's last known coordinates and velocity vector.
 * - Autonomous Reacquisition: If the emitter turns back on while the missile is within range and FOV, active tracking resumes.
 * - RWR Target Handoff: Slaves directly to threats detected by URadarWarningReceiverComponent.
 * - Pre-Briefed GPS Coordinates: Long-range standoff cruise toward known SAM site coordinates.
 * - Energy Management Loft: High-altitude ballistic arc for maximum kinetic standoff range.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UARMMissileGuidanceComponent : public UMissileGuidanceComponent
{
	GENERATED_BODY()

public:
	UARMMissileGuidanceComponent();

	/** Fired when missile guidance phase transitions */
	UPROPERTY(BlueprintAssignable, Category = "Missile|ARM|Events")
	FOnARMPhaseChangedSignature OnPhaseChanged;

	/** Fired when passive seeker confirms lock on an active RF emitter */
	UPROPERTY(BlueprintAssignable, Category = "Missile|ARM|Events")
	FOnEmitterAcquiredSignature OnEmitterAcquired;

	/** Fired when target emitter ceases radiation and missile enters dead reckoning */
	UPROPERTY(BlueprintAssignable, Category = "Missile|ARM|Events")
	FOnEmitterLostSignature OnEmitterLost;

	/** Fired when an emitter that previously shut down is reacquired */
	UPROPERTY(BlueprintAssignable, Category = "Missile|ARM|Events")
	FOnEmitterReacquiredSignature OnEmitterReacquired;

	/** Fired when emitter remains dark past the memory timeout */
	UPROPERTY(BlueprintAssignable, Category = "Missile|ARM|Events")
	FOnDeadReckoningTimeoutSignature OnDeadReckoningTimeout;

	/** Fired when target data is successfully handed off from RWR */
	UPROPERTY(BlueprintAssignable, Category = "Missile|ARM|Events")
	FOnRWRHandoffReceivedSignature OnRWRHandoffReceived;

	// --- Operational Configuration ---

	/** Operational attack mode for SEAD engagement */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Configuration")
	EARMGuidanceMode GuidanceMode = EARMGuidanceMode::TargetOfOpportunity;

	/** Half-angle in degrees of the passive RF seeker antenna detection cone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Seeker", meta = (ClampMin = "5.0", ClampMax = "90.0", UIMin = "10.0", UIMax = "60.0"))
	float PassiveSeekerConeAngle = 40.0f;

	/** Maximum mechanical / electronic gimbal limit angle in degrees from missile boresight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Seeker", meta = (ClampMin = "10.0", ClampMax = "90.0", UIMin = "20.0", UIMax = "75.0"))
	float GimbalLimitAngle = 60.0f;

	/** Maximum passive RF detection range in cm (passive reception enjoys 1/R^2 inverse-square sensitivity, ~150km default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Seeker", meta = (ClampMin = "100000.0", UIMin = "1000000.0"))
	float PassiveSeekerMaxRange = 15000000.0f;

	/** Minimum signal interval in seconds before declaring emitter shut down (absorbs radar antenna mechanical sweep periods) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Counter-Defense", meta = (ClampMin = "0.1", UIMin = "0.2", UIMax = "3.0"))
	float EmissionLossGracePeriod = 0.8f;

	/** Maximum duration in seconds missile will navigate toward last known coordinates after emitter shuts down */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Counter-Defense", meta = (ClampMin = "1.0", UIMin = "5.0", UIMax = "60.0"))
	float MemoryDriftTimeout = 15.0f;

	/** If true, missile will re-lock onto the emitter if it starts radiating again during dead reckoning */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Counter-Defense")
	bool bCanReacquireIfEmitterResumes = true;

	/** If true, prioritized targeting favours emitters actively in STT lock or tracking modes over passive search radars */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Seeker")
	bool bPrioritizeLockOnEmitters = true;

	/** Actor tags required on target emitter actor (if empty, all emitting radars are valid) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Seeker")
	TArray<FName> TargetEmitterFilterTags;

	// --- Loft Trajectory Configuration ---

	/** If true, missile climbs into upper atmosphere to conserve kinetic energy on long-range engagements */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Trajectory")
	bool bEnableLoft = true;

	/** Pitch-up angle in degrees during loft phase */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Trajectory", meta = (EditCondition = "bEnableLoft", ClampMin = "5.0", ClampMax = "45.0"))
	float LoftAngle = 20.0f;

	/** Minimum distance to target in cm required at launch to execute a loft maneuver (15 km default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Trajectory", meta = (EditCondition = "bEnableLoft", ClampMin = "100000.0"))
	float LoftMinLaunchRange = 1500000.0f;

	/** Range to target in cm at which loft ends and terminal dive commences (10 km default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Missile|ARM|Trajectory", meta = (EditCondition = "bEnableLoft", ClampMin = "50000.0"))
	float LoftTerminationRange = 1000000.0f;


	// --- Public API ---

	/**
	 * Slaves this missile to an emitter detected by the aircraft's Radar Warning Receiver (RWR).
	 *
	 * @param InRWR RWR component holding the threat table
	 * @param ThreatID Unique threat ID from the RWR
	 * @return True if threat was found, confirmed emitting, and locked
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|ARM|Handoff")
	bool HandoffFromRWR(URadarWarningReceiverComponent* InRWR, int32 ThreatID);

	/**
	 * Slaves this missile directly from an RWR threat entry struct.
	 *
	 * @param ThreatEntry RWR threat entry to engage
	 * @return True if threat source was valid and acquired
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|ARM|Handoff")
	bool HandoffFromRWRThreat(const FRWRThreatEntry& ThreatEntry);

	/**
	 * Directly designates an emitter actor and optional radar component to home upon.
	 *
	 * @param InEmitterActor Target actor containing emitting radar
	 * @param InRadarComp Optional specific radar component; if null, will auto-find on actor
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|ARM|Handoff")
	void HandoffEmitter(AActor* InEmitterActor, UAircraftRadarComponent* InRadarComp = nullptr);

	/**
	 * Configures target GPS/world coordinates for Pre-Briefed (PB) launch mode.
	 *
	 * @param InLocation World space coordinates of the target radar / SAM installation
	 */
	UFUNCTION(BlueprintCallable, Category = "Missile|ARM|Handoff")
	void SetPreBriefedTargetLocation(const FVector& InLocation);

	/** Returns true if the missile is actively tracking a radiating RF source */
	UFUNCTION(BlueprintPure, Category = "Missile|ARM|Status")
	bool IsTrackingActiveEmission() const;

	/** Returns true if navigating on coordinate memory / dead reckoning */
	UFUNCTION(BlueprintPure, Category = "Missile|ARM|Status")
	FORCEINLINE bool IsInDeadReckoning() const { return FlightPhase == EARMFlightPhase::DeadReckoning; }

	/** Returns the current flight phase */
	UFUNCTION(BlueprintPure, Category = "Missile|ARM|Status")
	FORCEINLINE EARMFlightPhase GetFlightPhase() const { return FlightPhase; }

	/** Returns the active guidance mode */
	UFUNCTION(BlueprintPure, Category = "Missile|ARM|Status")
	FORCEINLINE EARMGuidanceMode GetGuidanceMode() const { return GuidanceMode; }

	/** Returns the last known / dead-reckoned world position of the emitter */
	UFUNCTION(BlueprintPure, Category = "Missile|ARM|Status")
	FORCEINLINE FVector GetLastKnownEmitterLocation() const { return LastKnownEmitterLocation; }

	/** Returns the extrapolated velocity of the emitter */
	UFUNCTION(BlueprintPure, Category = "Missile|ARM|Status")
	FORCEINLINE FVector GetLastKnownEmitterVelocity() const { return LastKnownEmitterVelocity; }

	/** Returns the target radar component if currently known */
	UFUNCTION(BlueprintPure, Category = "Missile|ARM|Status")
	FORCEINLINE UAircraftRadarComponent* GetTargetRadarComponent() const { return TargetRadarComponent.Get(); }

	// --- Overrides ---
	virtual bool CanFireWeapon() const override;
	virtual bool FireWeapon() override;
	virtual bool PrepareLaunch(const FMissileLaunchConfiguration& Configuration) override;
	virtual void UpdateGuidanceVelocity(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickSeekerLogic(float DeltaTime) override;
	virtual bool GetGuidanceTargetSolution(FMissileTargetSolution& OutSolution) const override;

	/** Transitions to a new flight phase with delegate broadcast */
	void TransitionToPhase(EARMFlightPhase NewPhase);

	/** Evaluates candidate radar components in range to acquire the optimal emitter */
	void PerformPassiveSeekerScan();

	/** Checks whether a candidate radar is eligible (emitting, inside cone, friendly exclusion) */
	bool IsRadarCandidateEligible(UAircraftRadarComponent* Candidate, float& OutAngleDeg, float& OutDistCm) const;

	/** Verifies if the currently locked emitter is still actively emitting */
	void VerifyCurrentEmitterEmission(float DeltaTime);

	/** Computes commanded direction during high-altitude energy loft */
	FVector ComputeLoftDirection(const FVector& ForwardDir, const FVector& ToTarget) const;

private:
	/** Current operational flight phase */
	UPROPERTY(Transient, Replicated)
	EARMFlightPhase FlightPhase = EARMFlightPhase::PreLaunch;

	/** Cached weak pointer to the target radar component */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAircraftRadarComponent> TargetRadarComponent;

	/** Extrapolated world location of the emitter in dead reckoning mode */
	UPROPERTY(Transient, Replicated)
	FVector LastKnownEmitterLocation = FVector::ZeroVector;

	/** Extrapolated velocity vector of the emitter in dead reckoning mode */
	UPROPERTY(Transient)
	FVector LastKnownEmitterVelocity = FVector::ZeroVector;

	/** Pre-briefed GPS/world coordinates for PB mode */
	UPROPERTY(Transient, Replicated)
	FVector PreBriefedTargetLocation = FVector::ZeroVector;
	UPROPERTY(Transient, Replicated)
	bool bHasPreBriefedTarget = false;
	UPROPERTY(Transient, Replicated)
	bool bHasEmitterMemory = false;

	/** Elapsed time since the active RF signal was lost */
	UPROPERTY(Transient)
	float TimeSinceEmissionLost = 0.0f;

	/** Elapsed time spent in dead reckoning phase */
	UPROPERTY(Transient)
	float DeadReckoningElapsedTime = 0.0f;

	/** Cached threat ID if handed off from RWR */
	UPROPERTY(Transient)
	int32 HandoffThreatID = INDEX_NONE;

	/** Flag indicating whether the initial loft climb has finished */
	UPROPERTY(Transient)
	bool bLoftComplete = false;
};

// Aliases for convenience and backward compatibility
using EARMGuidanceMode_Aliases = EARMGuidanceMode;
using EAntiRadiationGuidanceMode = EARMGuidanceMode;
using EAntiRadiationFlightPhase = EARMFlightPhase;
using UAntiRadiationMissileGuidanceComponent = UARMMissileGuidanceComponent;
using UARMGuidanceComponent = UARMMissileGuidanceComponent;
