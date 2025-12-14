// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "CombatTeamUtility.h"
#include "AircraftCombatCommonTypes.h"
#include "AircraftCombatSettings.h"
#include "RadarWarningReceiverComponent.generated.h"

class UAircraftRadarComponent;
class URadarMissileGuidanceComponent;


/**
 * Individual threat entry detected by the Radar Warning Receiver
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FRWRThreatEntry
{
	GENERATED_BODY()

	/** Unique RWR threat entry ID */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	int32 ThreatID = 0;

	/** Actor that owns the emitting radar or missile seeker */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	TWeakObjectPtr<AActor> SourceActor;

	/** Threat classification level */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	ERWRThreatType ThreatType = ERWRThreatType::None;

	/** Relative bearing to the threat emitter in degrees (0 = nose, 90 = right wing, 180 = tail) */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	float BearingDegrees = 0.0f;

	/** Normalized signal strength (0.0 = barely detectable, 1.0 = strong) */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	float SignalStrength = 0.0f;

	/** Estimated range to emitter in cm (may not always be determinable) */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	float Range = 0.0f;

	/** World time when this threat was first detected */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	float TimeDetected = 0.0f;

	/** World time when this threat was last updated */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	float TimeLastUpdated = 0.0f;

	/** World time when RF signal (sweep or lock) was last received from this emitter */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	float LastSignalTime = 0.0f;

	/** World time when high-threat illumination (STT Lock or CW Launch) was last received from this emitter */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	float LastLockTime = 0.0f;

	/** Operational domain / vehicle type of the threat (Air, Ground, Sea, Missile) for HUD/MFD surrounding symbology */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	ERadarTargetDomain VehicleType = ERadarTargetDomain::Air;

	/** True if this entry represents an autonomous active radar missile seeker (Pitbull / Maddog) */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	bool bIsActiveMissile = false;

	/** True if this threat was just detected this scan cycle */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	bool bIsNewThreat = false;

	/** True for ONLY the single highest-priority threat across the entire detected list (the Diamond Threat) */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	bool bHighestThreatAvailable = false;

	/** True only during the update cycle in which this threat escalated in severity; returns to false on next update */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	bool bIsEscalated = false;

	/** True only during the update cycle in which this threat de-escalated in severity; returns to false on next update */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	bool bIsDeescalated = false;

	/** Emitter type tag for symbology (e.g. "SA-10", "F-16", "AIM-120", "M") */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	FName EmitterType = NAME_None;

	/** Returns true if this threat entry is an active radar missile */
	FORCEINLINE bool IsMissile() const { return bIsActiveMissile || ThreatType == ERWRThreatType::MissileSeeker; }

	/** Returns true if this threat is in launch warning status (CW or active seeker) */
	FORCEINLINE bool IsLaunchWarning() const { return ThreatType == ERWRThreatType::MissileLaunch || ThreatType == ERWRThreatType::MissileSeeker; }

	/** Returns true if this threat is in lock-on or launch status */
	FORCEINLINE bool IsLock() const { return ThreatType >= ERWRThreatType::LockOnRadar; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRWRThreatDetectedSignature, const FRWRThreatEntry&, Threat);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRWRThreatUpdatedSignature, const FRWRThreatEntry&, Threat);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRWRThreatEscalatedSignature, const FRWRThreatEntry&, Threat);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRWRThreatDeescalatedSignature, const FRWRThreatEntry&, Threat);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRWRThreatLostSignature, int32, ThreatID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRWRMissileLaunchDetectedSignature, const FRWRThreatEntry&, Threat);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRWRAllThreatsCleared);

/**
 * Radar Warning Receiver (RWR) Component
 *
 * Passive electronic warfare sensor that detects when the owning aircraft is being
 * illuminated by radar systems or targeted by active-radar-guided missiles.
 *
 * Periodically scans for nearby UAircraftRadarComponent and URadarMissileGuidanceComponent
 * instances, classifies the threat level, and broadcasts delegates for game-layer
 * response (cockpit tones, display symbology, countermeasure dispensing).
 *
 * Designed for fighter aircraft RWR systems (ALR-56, ALR-67, ALR-69A).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API URadarWarningReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URadarWarningReceiverComponent();

	/** New threat source detected (play search warning tone) */
	UPROPERTY(BlueprintAssignable, Category = "RWR|Events")
	FOnRWRThreatDetectedSignature OnThreatDetected;

	/** Existing threat status changed */
	UPROPERTY(BlueprintAssignable, Category = "RWR|Events")
	FOnRWRThreatUpdatedSignature OnThreatUpdated;

	/** Threat level escalated (e.g., search -> lock-on or lock-on -> missile launch — play high-priority tone) */
	UPROPERTY(BlueprintAssignable, Category = "RWR|Events")
	FOnRWRThreatEscalatedSignature OnThreatEscalated;

	/** Threat level de-escalated (e.g. lock-on broken -> search or search -> none) */
	UPROPERTY(BlueprintAssignable, Category = "RWR|Events")
	FOnRWRThreatDeescalatedSignature OnThreatDeescalated;

	/** Threat signal timed out / lost */
	UPROPERTY(BlueprintAssignable, Category = "RWR|Events")
	FOnRWRThreatLostSignature OnThreatLost;

	/** Dedicated missile launch warning (critical alert — play missile warning tone) */
	UPROPERTY(BlueprintAssignable, Category = "RWR|Events")
	FOnRWRMissileLaunchDetectedSignature OnMissileLaunchDetected;

	/** All active threats cleared */
	UPROPERTY(BlueprintAssignable, Category = "RWR|Events")
	FOnRWRAllThreatsCleared OnAllThreatsCleared;

	/** RWR detection sensitivity (multiplier for detection range, default 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float RWRSensitivity = 1.0f;

	/** How often the RWR scans for threats in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration", meta = (ClampMin = "0.1"))
	float RWRUpdateInterval = 0.5f;

	/** How long a threat persists after last signal before being dropped (legacy fallback, see SearchThreatTimeoutSeconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration", meta = (ClampMin = "1.0"))
	float ThreatTimeoutSeconds = 5.0f;

	/** How long a sweeping Search radar persists without receiving hits before being dropped (seconds, real-world ~6-8s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration|Decay", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float SearchThreatTimeoutSeconds = 7.0f;

	/** Grace period in seconds before a lost Lock or Launch illumination de-escalates back to Search state (real-world ~1-2s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration|Decay", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float LockLossGracePeriod = 1.5f;

	/** How long an autonomous active missile seeker track persists after signal loss before being dropped (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration|Decay", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MissileSeekerTimeoutSeconds = 2.0f;

	/** If true, checks terrain line-of-sight raycasts to determine if static terrain masks/blocks the incoming RF signal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration")
	bool bEnableTerrainMasking = true;

	/** Collision channel used for terrain masking line-of-sight raycasts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration", meta = (EditCondition = "bEnableTerrainMasking"))
	TEnumAsByte<ECollisionChannel> LineOfSightChannel = ECC_Visibility;

	/** Maximum detection range for incoming radar signals in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration", meta = (ClampMin = "100000.0"))
	float MaxRWRRange = 20000000.0f;

	/** Collision channel used for detecting radar sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration")
	TEnumAsByte<ECollisionChannel> RWRDetectionChannel = ECC_Pawn;

	/** Actor tags whose radar emissions are ignored by this RWR (e.g. "Friendly") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration")
	TArray<FName> RWRIgnoreTags;

	/** If true, uses IGenericTeamAgentInterface to identify friendly radar emitters.
	 *  Enable for arcade gameplay where players should see IFF-tagged contacts.
	 *  Disable for realism where the RWR treats all emitters as potential threats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|IFF")
	bool bEnableIFF = true;

	/** If true, friendly radar emissions are completely hidden from the RWR (not displayed at all).
	 *  If false, they appear as FriendlyRadar threat type so the pilot knows they're being painted by wingman/AWACS. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|IFF", meta = (EditCondition = "bEnableIFF"))
	bool bHideFriendlyEmitters = false;

	/** If true, evaluates all threats against Project Settings priority rankings and designates the single most dangerous threat as the Diamond Threat (bHighestThreatAvailable).
	 *  If false, bHighestThreatAvailable remains false for all threats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration")
	bool bFindHighestThreatAvailable = true;

	/** Master switch for debug visualization and HUD telemetry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Debug")
	bool bEnableDebugTraces = false;

	/** If true, renders 3D threat strobes and bearing lines */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Debug", meta = (EditCondition = "bEnableDebugTraces"))
	bool bDrawThreatStrobes = true;

	/** If true, renders on-screen RWR telemetry diagnostics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Debug", meta = (EditCondition = "bEnableDebugTraces"))
	bool bEnableDiagnosticHUD = true;

	/** Returns all active RWR threat entries */
	UFUNCTION(BlueprintPure, Category = "RWR|Threats")
	const TArray<FRWRThreatEntry>& GetAllThreats() const { return ThreatEntries; }

	/** Returns threats filtered by type */
	UFUNCTION(BlueprintPure, Category = "RWR|Threats")
	void GetThreatsByType(ERWRThreatType InType, TArray<FRWRThreatEntry>& OutThreats) const;

	/** Returns the highest priority threat */
	UFUNCTION(BlueprintPure, Category = "RWR|Threats")
	bool GetHighestThreat(FRWRThreatEntry& OutThreat) const;

	/** Returns the total number of active threats */
	UFUNCTION(BlueprintPure, Category = "RWR|Threats")
	FORCEINLINE int32 GetThreatCount() const { return ThreatEntries.Num(); }

	/** Returns true if any threat is currently in lock-on status */
	UFUNCTION(BlueprintPure, Category = "RWR|Threats")
	bool HasActiveLockOnThreat() const;

	/** Returns true if any threat is a missile launch warning */
	UFUNCTION(BlueprintPure, Category = "RWR|Threats")
	bool HasMissileLaunchWarning() const;

	/** Returns the relative bearing to a specific threat in degrees */
	UFUNCTION(BlueprintPure, Category = "RWR|Threats")
	float GetBearingToThreat(int32 ThreatID) const;

	/** Returns the highest threat level currently detected */
	UFUNCTION(BlueprintPure, Category = "RWR|Threats")
	ERWRThreatType GetHighestThreatLevel() const;

	/**
	 * Calculates normalized signal strength (0.0 to 1.0) with quadratic distance attenuation.
	 * Returns 1.0 at point-blank range, smoothly falling off to 0.0 at MaxRange.
	 */
	UFUNCTION(BlueprintPure, Category = "RWR|Signal")
	static float CalculateSignalStrength(float Range, float MaxRange);

	/**
	 * Resolves the emitter type identifier (e.g. "F-16", "SA-10", "AIM-120") from a source actor's tags.
	 * Looks for tags matching 'EmitterType=X' or 'EmitterType:X' (or 'Emitter=X').
	 * Falls back to DefaultFallback if no matching tag is found.
	 */
	FName ResolveEmitterType(const AActor* SourceActor, FName DefaultFallback = NAME_None) const;

	/**
	 * Checks if the radio-frequency line of sight is clear between emitter location and RWR receiver (unblocked by terrain geometry).
	 * Returns true if clear, false if blocked by terrain.
	 */
	UFUNCTION(BlueprintPure, Category = "RWR|Signal")
	bool IsSignalLineOfSightClear(const FVector& EmitterLocation, const AActor* EmitterActor = nullptr) const;


protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** All active threat entries */
	UPROPERTY(Transient, ReplicatedUsing = OnRep_ThreatEntries)
	TArray<FRWRThreatEntry> ThreatEntries;

	UFUNCTION()
	void OnRep_ThreatEntries();

	UPROPERTY(Transient)
	TArray<FRWRThreatEntry> PreviousThreatEntries;

	/** Time accumulator for scan interval throttling */
	UPROPERTY(Transient)
	float ScanAccumulator = 0.0f;

	/** Monotonically increasing threat ID counter */
	UPROPERTY(Transient)
	int32 NextThreatID = 1;

	/** Performs the periodic RWR scan for radar emissions and missile seekers */
	void PerformRWRScan();

	/** Classifies the threat level from a radar component targeting this aircraft */
	ERWRThreatType ClassifyRadarThreat(const UAircraftRadarComponent* RadarComp) const;

	/** Checks if the owning aircraft is in a radar's track list */
	bool IsInRadarTrackList(const UAircraftRadarComponent* RadarComp) const;

	/** Scans for active radar missile seekers targeting this aircraft */
	void ScanForMissileSeekers();

	/** Updates bearing and signal strength for existing threats */
	void UpdateExistingThreats();

	/** Prunes threats that have timed out */
	void PruneStaleThreats();

	/** Evaluates active threats against project settings priority rankings and designates the single highest threat */
	void EvaluateHighestThreat();

	/** Computes the relative bearing from this aircraft to a world position */
	float ComputeRelativeBearing(const FVector& SourcePosition) const;

	/** Finds the threat entry index for a given source actor. Returns INDEX_NONE if not found. */
	int32 FindThreatIndex(const AActor* SourceActor) const;

	/** Returns true if a source actor should be ignored */
	bool ShouldIgnoreSource(const AActor* SourceActor) const;

	/** Debug rendering */
	void DrawDebugThreats() const;
};
