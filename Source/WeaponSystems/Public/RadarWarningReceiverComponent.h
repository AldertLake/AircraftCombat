// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "RadarWarningReceiverComponent.generated.h"

class UAircraftRadarComponent;
class URadarMissileGuidanceComponent;

/**
 * Threat classification level detected by the RWR
 */
UENUM(BlueprintType)
enum class ERWRThreatType : uint8
{
	None UMETA(DisplayName = "No Threat"),
	FriendlyRadar UMETA(DisplayName = "Friendly Radar (IFF Cleared)"),
	SearchRadar UMETA(DisplayName = "Search Radar"),
	TrackingRadar UMETA(DisplayName = "Tracking Radar (TWS)"),
	LockOnRadar UMETA(DisplayName = "Lock-On Radar (STT)"),
	MissileSeeker UMETA(DisplayName = "Missile Seeker (Active Radar)"),
	MissileLaunch UMETA(DisplayName = "Missile Launch Detected")
};

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

	/** True if this threat was just detected this scan cycle */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	bool bIsNewThreat = false;

	/** True if this is a high-priority threat (lock-on or missile launch) */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	bool bIsCritical = false;

	/** Emitter type tag for symbology (e.g. "SA-10", "F-16", "AIM-120") */
	UPROPERTY(BlueprintReadOnly, Category = "RWR|Threat")
	FName EmitterType = NAME_None;
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

	/** How long a threat persists after last signal before being dropped (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration", meta = (ClampMin = "1.0"))
	float ThreatTimeoutSeconds = 5.0f;

	/** Maximum detection range for incoming radar signals in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration", meta = (ClampMin = "100000.0"))
	float MaxRWRRange = 20000000.0f;

	/** Collision channel used for detecting radar sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration")
	TEnumAsByte<ECollisionChannel> RWRDetectionChannel = ECC_Pawn;

	/** Actor tags whose radar emissions are ignored by this RWR (e.g. "Friendly") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration")
	TArray<FName> RWRIgnoreTags;

	/** Actor tags that identify missile actors for seeker detection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Configuration")
	TArray<FName> MissileActorTags;

	/** Master switch for debug visualization and HUD telemetry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Debug")
	bool bEnableDebugTraces = false;

	/** If true, renders 3D threat strobes and bearing lines */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Debug")
	bool bDrawThreatStrobes = true;

	/** If true, renders on-screen RWR telemetry diagnostics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RWR|Debug")
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

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** All active threat entries */
	UPROPERTY(Transient, Replicated)
	TArray<FRWRThreatEntry> ThreatEntries;

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

	/** Computes the relative bearing from this aircraft to a world position */
	float ComputeRelativeBearing(const FVector& SourcePosition) const;

	/** Finds the threat entry index for a given source actor. Returns INDEX_NONE if not found. */
	int32 FindThreatIndex(const AActor* SourceActor) const;

	/** Returns true if a source actor should be ignored */
	bool ShouldIgnoreSource(const AActor* SourceActor) const;

	/** Debug rendering */
	void DrawDebugThreats() const;
};
