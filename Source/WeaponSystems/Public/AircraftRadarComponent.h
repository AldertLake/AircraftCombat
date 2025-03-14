// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "CombatTeamUtility.h"
#include "AircraftCombatCommonTypes.h"
#include "AircraftCombatSettings.h"
#include "RadarDisplayGeometry.h"
#include "RadarMissileGuidanceComponent.h"
#include "AircraftRadarComponent.generated.h"

class AActor;
class APawn;
class USceneComponent;
class UAircraftRadarComponent;
class URadarMissileGuidanceComponent;
class APlayerController;
class ARadarOperatorLink;
class UAircraftDataLinkSubsystem;
struct FRadarOperatorSnapshot;

UENUM(BlueprintType)
enum class ERadarScanDrive : uint8
{
	VirtualMechanical UMETA(DisplayName = "Virtual Mechanical Sweep"),
	SocketDriven UMETA(DisplayName = "Rotating Socket Forward Beam"),
	PESA UMETA(DisplayName = "PESA Electronic Steering"),
	AESA UMETA(DisplayName = "AESA Electronic Steering")
};

UENUM(BlueprintType)
enum class ERadarCommandType : uint8
{
	SetMode, SetRange, SetACMMode, LockTrack, BugTrack, BreakLock, ClearBug,
	DesignateCursor, SetCursor, SetCursorInput, SetScanVolume, OffsetScanCenter,
	DesignateSpotlight, ClearSpotlight, HelmetCue, ApplyScanPreset, DesignateLinkedTrack,
	SetDisplayWindow, SetDisplayGeometry, SetDisplayHeadingUp
};

UENUM(BlueprintType)
enum class EDataLinkPlatformType : uint8
{
	Unknown, AirborneC2, Fighter, Surface, Ground
};

UENUM(BlueprintType)
enum class ERadarTrackSource : uint8
{
	Local, Unknown, AirborneC2, Fighter, Surface, Ground
};

/**
 * Radar operating mode determining scan behavior and track management
 */
UENUM(BlueprintType)
enum class ERadarOperatingMode : uint8
{
	Off UMETA(DisplayName = "OFF (EMCON Silent)"),
	Standby UMETA(DisplayName = "STANDBY (Powered, No Emission)"),
	Search UMETA(DisplayName = "RWS (Range While Search)"),
	TrackWhileScan UMETA(DisplayName = "TWS (Track While Scan)"),
	SingleTargetTrack UMETA(DisplayName = "STT (Single Target Track)"),
	AirCombatManeuver UMETA(DisplayName = "ACM (Air Combat Maneuver)"),
	GroundMapping UMETA(DisplayName = "GM (Ground Mapping)"),
	SeaSearch UMETA(DisplayName = "SS (Sea Search)"),
	Spotlight UMETA(DisplayName = "SAR (Spotlight / Synthetic Aperture)")
};

/**
 * ACM sub-mode for close-range automatic acquisition
 */
UENUM(BlueprintType)
enum class ERadarACMSubMode : uint8
{
	Boresight UMETA(DisplayName = "Boresight (20° Cone Ahead)"),
	VerticalScan UMETA(DisplayName = "Vertical Scan (10°W x -10° to +60°V)"),
	HelmetCue UMETA(DisplayName = "Helmet Cue (HMD Slaved)"),
	SlewAcquisition UMETA(DisplayName = "Slew Acquisition (TDC Steered)")
};

/**
 * Status of a radar track file
 */
UENUM(BlueprintType)
enum class ERadarTrackStatus : uint8
{
	Search UMETA(DisplayName = "Search Hit"),
	Tracked UMETA(DisplayName = "Tracked (TWS)"),
	Locked UMETA(DisplayName = "Locked (STT)"),
	Bugged UMETA(DisplayName = "Bugged (TWS Priority)"),
	Lost UMETA(DisplayName = "Lost / Fading"),
	Jammed UMETA(DisplayName = "Jammed / ECM")
};

/**
 * IFF classification result
 */
UENUM(BlueprintType)
enum class ERadarIFFResult : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	Friendly UMETA(DisplayName = "Friendly"),
	Hostile UMETA(DisplayName = "Hostile"),
	Neutral UMETA(DisplayName = "Neutral")
};


/**
 * Preset radar scan azimuth widths
 */
UENUM(BlueprintType)
enum class ERadarScanSize : uint8
{
	Narrow_20 UMETA(DisplayName = "20° Narrow"),
	Medium_40 UMETA(DisplayName = "40° Medium"),
	Wide_60 UMETA(DisplayName = "60° Wide"),
	Full_120 UMETA(DisplayName = "120° Full"),
	Custom UMETA(DisplayName = "Custom")
};

/**
 * Individual radar track file representing a detected and/or tracked contact
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FRadarTrack
{
	GENERATED_BODY()

	/** Unique track file number assigned by the radar */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	int32 TrackID = 0;

	/** Stable identity assigned by this receiver; several source tracks may share it. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	int32 ContactID = 0;

	/** Underlying actor being tracked (may be null for unresolved contacts) */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	TWeakObjectPtr<AActor> TrackedActor;

	/** Current status of this track */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarTrackStatus Status = ERadarTrackStatus::Search;

	/** IFF classification */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarIFFResult IFFResult = ERadarIFFResult::Unknown;

	/** Local is assigned only to measurements made by this radar. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarTrackSource Source = ERadarTrackSource::Local;

	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	int32 SourceParticipantID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	int32 SourceTrackID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bHasActorAssociation = false;

	/** Last measured world position */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	FVector LastKnownPosition = FVector::ZeroVector;

	/** Smoothed velocity estimate */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	FVector EstimatedVelocity = FVector::ZeroVector;

	/** Radial closure velocity toward the radar in cm/s (positive = closing) */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float ClosureRate = 0.0f;

	/** Azimuth bearing from aircraft nose in degrees (-180 to 180) */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float Bearing = 0.0f;

	/** Elevation angle from aircraft horizon in degrees */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float Elevation = 0.0f;

	/** Slant range in cm */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float Range = 0.0f;

	/** Normalized signal strength / quality (0.0 = barely detectable, 1.0 = strong return) */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float SignalStrength = 0.0f;

	/** Time in seconds since the last valid radar return for this track */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float TrackAge = 0.0f;

	/** Target altitude above sea level in cm */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float AltitudeASL = 0.0f;

	/** Estimated target heading in degrees (0-360) */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float TargetHeading = 0.0f;

	/** True if target is in the Doppler notch (perpendicular crossing) */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsNotching = false;

	/** True if ECM jamming is detected from this contact */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsJamming = false;

	/** True if this track is the STT beam target or TWS bugged priority */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsBeamTarget = false;

	/** True if this track is currently bugged (PDT priority in TWS) */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsBugged = false;

	/** Detected or assigned Radar Cross Section in m² */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float RCS = 0.0f;

	/** Aspect-modified effective Radar Cross Section in m² */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float EffectiveRCS = 0.0f;

	/** True if target provided an explicit 'RCS=X' tag; false if using the unnatural fallback */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bHasRCSTag = false;

	/** Operational domain of this target (Air, Ground, Sea) determined by tag classification */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarTargetDomain TargetDomain = ERadarTargetDomain::Air;

	/** True if this track was detected as a surface/ground/naval contact */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsGroundTarget = false;

	/** True if surface contact is a moving ground vehicle above the GMTI velocity threshold */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsGMTIMoving = false;
};

UENUM(BlueprintType)
enum class ERadarMissileGuidanceSource : uint8
{
	Inertial, DataLink, Illumination, OnboardSeeker, Unguided
};

/** One launched radar missile; transient estimates are refreshed when queried or sent to crew. */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FRadarLaunchedMissileStatus
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 MissileID = 0;
	/** May be null on a client if the missile actor is not network relevant. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<URadarMissileGuidanceComponent> MissileComponent = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<AActor> MissileActor = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<UAircraftRadarComponent> LaunchRadar = nullptr;
	/** Active external provider, or the last one after onboard takeover; otherwise launch radar. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<UAircraftRadarComponent> RelevantRadar = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<UAircraftRadarComponent> CurrentExternalRadar = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<UAircraftRadarComponent> LastExternalRadar = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 LaunchTrackID = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 ContactID = 0;
	/** Reporter identity stays available if the reporter component is not network relevant. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 SourceParticipantID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 SourceTrackID = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") bool bMadDog = false;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") ERadarMissileFlightPhase FlightPhase = ERadarMissileFlightPhase::PreLaunch;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") ERadarMissileGuidanceSource GuidanceSource = ERadarMissileGuidanceSource::Inertial;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") bool bOnboardSeekerActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") float FlightTimeSeconds = 0.0f;
	/** -1: no two-phase seeker or no usable estimate; 0: active after a two-phase transition. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") float TimeToActiveSeconds = -1.0f;
	/** -1 when target state or positive closure is unavailable. An estimate, never a hit promise. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") float EstimatedTimeToImpactSeconds = -1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") float EstimatedTargetRangeCm = -1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") float ClosingSpeedCmPerSecond = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") bool bHasTargetSolution = false;
};

UENUM()
enum class ERadarOperatorEventType : uint8
{
	LockAcquired, LockLost, CursorDesignated, SARReady
};

/** Recent discrete events retained across coalesced operator snapshots. */
USTRUCT()
struct FRadarOperatorEvent
{
	GENERATED_BODY()
	UPROPERTY() int32 Sequence = 0;
	UPROPERTY() ERadarOperatorEventType Type = ERadarOperatorEventType::LockLost;
	UPROPERTY() int32 TrackID = -1;
	UPROPERTY() bool bSuccess = false;
	UPROPERTY() FRadarTrack Track;
	UPROPERTY() FVector Location = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSARImageReadySignature, const FVector&, PatchLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarContactNewSignature, const FRadarTrack&, NewTrack);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarContactUpdatedSignature, const FRadarTrack&, UpdatedTrack);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarContactLostSignature, int32, TrackID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarLockAcquiredSignature, const FRadarTrack&, LockedTrack);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarLockLostByIDSignature, int32, TrackID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarModeChangedSignature, ERadarOperatingMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnScanSweepCompleteSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarTargetNotchingSignature, const FRadarTrack&, NotchingTrack);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRadarAllContactsClearedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarTrackSelectedSignature, const FRadarTrack&, SelectedTrack);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarTrackDeselectedSignature, int32, TrackID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnRadarScanProgressSignature, int32, CurrentBar, float, BarLevel, float, SweepLevel, bool, bScanningRight);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarCursorMovedSignature, const FRadarCursorState&, Cursor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRadarCursorDesignatedSignature, bool, bSuccess, int32, DesignatedTrackID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarDisplayRangeChangedSignature, float, NewDisplayRangeCm);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRadarSnapshotReadySignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadarDisplayStateUpdatedSignature, int32, Revision);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnRadarCommandResultSignature, int32, RequestID, ERadarCommandType, Command, bool, bSuccess, int32, TrackID);

/**
 * Aircraft Radar Component
 *
 * Full-fidelity airborne radar simulation supporting multiple operating modes
 * (RWS, TWS, STT, ACM, GM, SS), realistic antenna sweep simulation,
 * Doppler notch filtering, track file management, and configurable scan volumes.
 *
 * Designed for integration with fighter aircraft (AN/APG-68, APG-73, APG-77, APG-83),
 * AEW platforms, and ground-based air defense radars.
 *
 * Split implementation across multiple .cpp files:
 * - AircraftRadarComponent.cpp: Core logic, mode management, replication, helpers
 * - AircraftRadarComponent_Scan.cpp: Antenna sweep simulation, detection model
 * - AircraftRadarComponent_Tracking.cpp: Track file management, STT/TWS lock logic
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UAircraftRadarComponent : public UActorComponent, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FRadarDisplaySelectionTest;
	friend class FAircraftDataLinkRoutingTest;
#endif

public:
	UAircraftRadarComponent();

	/** Server-controlled radio configuration. An unassigned network never shares traffic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Data Link")
	FName DataLinkNetworkID = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link")
	bool bEnableDataLink = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link")
	bool bContributeDataLinkTracks = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link")
	bool bReceiveDataLinkTracks = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link")
	bool bRelayDataLinkReports = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link")
	bool bAllowRemoteWeaponSupport = false;
	/** Correlate only reports with the same server-verified actor association. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link|Track Correlation")
	bool bEnableTrackCorrelation = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link|Track Correlation", meta = (ClampMin = "0.1"))
	float LocalCorrelationFreshnessSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link")
	bool bRestrictDataLinkToFriendly = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (ClampMin = "1000.0"))
	float DataLinkRangeCm = 20000000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (ClampMin = "0.1"))
	float DataLinkDesyncSeconds = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (ClampMin = "0.1"))
	float DataLinkTrackExpirySeconds = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (ClampMin = "1"))
	int32 MaxLinkedDataLinkTracks = 128;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link")
	EDataLinkPlatformType DataLinkPlatformType = EDataLinkPlatformType::Unknown;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Data Link")
	void SetDataLinkNetworkID(FName NewNetworkID);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Data Link")
	void SetDataLinkContributionEnabled(bool bEnabled);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Data Link")
	void SetDataLinkReceptionEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	bool IsDataLinkConnected() const;
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	int32 GetDataLinkParticipantID() const { return DataLinkParticipantID; }
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	void GetLinkedTracks(TArray<FRadarTrack>& OutTracks) const { OutTracks = LinkedTracks; }
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	void GetDisplayTracks(TArray<FRadarTrack>& OutTracks) const;
	/** Best fresh measured source for a verified actor, preferring this radar over donors. */
	bool GetBestWeaponSupportTrackForActor(const AActor* Actor, FRadarTrack& OutTrack) const;
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link|Track Correlation")
	int32 GetSelectedContactID() const { return SelectedContactID; }
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	bool GetLinkedTrackByID(int32 TrackID, FRadarTrack& OutTrack) const;
	/** Server-only actor association and original reporting radar lookup. */
	bool GetFreshLinkedTrackForActor(const AActor* Actor, int32 OriginID, FRadarTrack& OutTrack) const;
	UAircraftRadarComponent* GetLinkedTrackSource(int32 TrackID) const;
	bool GetSelectedLinkedTrackForActor(const AActor* Actor, FRadarTrack& OutTrack) const;
	void ClearLinkedDesignation() { SelectedLinkedTrackID = -1; if (STTLockedTrackID < 0 && BuggedTrackID < 0) SelectedContactID = 0; }
	int32 GetSelectedLinkedTrackID() const { return SelectedLinkedTrackID; }
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Data Link")
	bool DesignateLinkedTrack(int32 TrackID);
	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestDesignateLinkedTrack(int32 TrackID, APlayerController* RequestingController);
	bool CanDataLinkTransmit() const;
	bool CanDataLinkReceive() const;
	bool CanDataLinkRelay() const;
	/** Per-node radio inhibition hook for future jammer effects. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Data Link")
	void SetDataLinkRadioInhibited(bool bTransmitInhibited, bool bReceiveInhibited);
	void SetDataLinkParticipantID(int32 NewID) { DataLinkParticipantID = NewID; }
	void ReceiveDataLinkHeartbeat(float WorldTime);
	void ReceiveDataLinkReport(const FRadarTrack& Report, UAircraftRadarComponent* SourceRadar, float MeasurementTime, float WorldTime);

	/** New radar contact detected */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarContactNewSignature OnRadarContactNew;

	/** Existing track file updated with fresh data */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarContactUpdatedSignature OnRadarContactUpdated;

	/** Track file dropped (timeout, out of range, or notched) */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarContactLostSignature OnRadarContactLost;

	/** All tracks cleared (e.g. radar powered down or mode reset) */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarAllContactsClearedSignature OnRadarAllContactsCleared;

	/** STT lock successfully acquired on a target */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarLockAcquiredSignature OnRadarLockAcquired;

	/** STT lock broken (mode change, target lost, or manual break) */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarLockLostByIDSignature OnRadarLockLost;

	/** Radar operating mode changed */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarModeChangedSignature OnRadarModeChanged;

	/** Full scan sweep (frame) completed */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnScanSweepCompleteSignature OnScanSweepComplete;

	/** Target entered Doppler notch */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarTargetNotchingSignature OnRadarTargetNotching;

	/** Target track selected by pilot (bugged in TWS/Search or locked in STT) */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarTrackSelectedSignature OnRadarTrackSelected;

	/** Target track deselected / cleared */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarTrackDeselectedSignature OnRadarTrackDeselected;

	/** Authoritative scan samples; local operators receive the latest sample after each versioned display snapshot. */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarScanProgressSignature OnScanProgressUpdated;

	/** Broadcasts radar-space cursor azimuth, elevation, and slant range after an authoritative change. */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarCursorMovedSignature OnRadarCursorMoved;

	/** Broadcasts when target designation is triggered under the cursor, indicating success and designated TrackID (-1 if none) */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarCursorDesignatedSignature OnRadarCursorDesignated;

	/** Broadcasts when the radar display range scale changes */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarDisplayRangeChangedSignature OnRadarDisplayRangeChanged;

	/** Broadcasts when a synthetic aperture radar (SAR) Spotlight image dwell integration completes */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnSARImageReadySignature OnSARImageReady;

	/** Initial operator state has arrived; widgets should hydrate from getters. */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarSnapshotReadySignature OnRadarSnapshotReady;

	/** Coherent state refresh for UI; late subscribers should first query getters. */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarDisplayStateUpdatedSignature OnRadarDisplayStateUpdated;

	/** A server-validated control request has completed. */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnRadarCommandResultSignature OnRadarCommandResult;

	/** Current radar operating mode */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_RadarMode, Category = "Radar|Configuration")
	ERadarOperatingMode RadarMode = ERadarOperatingMode::Search;

	UFUNCTION()
	void OnRep_RadarMode();

	/** Socket name on the aircraft mesh where the radar antenna/source is located. If None or not found, falls back to the aircraft root (0, 0, 0) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Configuration", meta = (DisplayName = "Radar Socket"))
	FName RadarSocketName = NAME_None;

	/** Stable bearing/UI frame. None uses the owning actor root; it must not rotate with the antenna. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Configuration")
	FName RadarReferenceComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume")
	ERadarScanDrive ScanDrive = ERadarScanDrive::VirtualMechanical;

	/** Local translation offset applied to radar source location (useful for elevating antenna origin on ground vehicles/turrets without custom mesh sockets) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Configuration")
	FVector RadarLocationOffset = FVector::ZeroVector;

	/** If true, this radar is ground/surface-based or turret-mounted with full hemispherical/360° tracking coverage, bypassing chassis-relative antenna gimbal break-locks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Configuration")
	bool bOmnidirectionalTracking = false;

	/** ACM sub-mode (only used when RadarMode == ACM) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Configuration", meta = (EditCondition = "RadarMode == ERadarOperatingMode::AirCombatManeuver", EditConditionHides))
	ERadarACMSubMode ACMSubMode = ERadarACMSubMode::Boresight;

	/** Scan size preset for quick azimuth configuration */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume")
	ERadarScanSize ScanSizePreset = ERadarScanSize::Wide_60;

	/** Total azimuth scan width in degrees (e.g. 120 for fighter, 360 for AEW). Locked to preset unless Custom is selected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume", meta = (ClampMin = "5.0", ClampMax = "360.0", EditCondition = "ScanSizePreset == ERadarScanSize::Custom", EditConditionHides))
	float AzimuthScanWidth = 120.0f;

	/** Total elevation scan height in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume", meta = (ClampMin = "2.0", ClampMax = "120.0"))
	float ElevationScanHeight = 20.0f;

	/** Number of elevation bar scan lines (1, 2, 4, 6, 8) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume", meta = (ClampMin = "1", ClampMax = "8"))
	int32 ElevationBars = 4;

	/** Scan center azimuth offset in degrees (TDC cursor slew) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume")
	float ScanCenterAzimuth = 0.0f;

	/** Scan center elevation offset in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume")
	float ScanCenterElevation = 0.0f;

	/** Maximum physical antenna gimbal limit in azimuth (degrees from antenna boresight, e.g. ±60°). Governs STT tracking lock bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Scan Volume", meta = (ClampMin = "10.0", ClampMax = "180.0"))
	float MaxAntennaGimbalAzimuth = 60.0f;

	/** Maximum physical antenna gimbal limit in elevation (degrees from antenna boresight, e.g. ±60°). Governs STT tracking lock bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Scan Volume", meta = (ClampMin = "10.0", ClampMax = "90.0"))
	float MaxAntennaGimbalElevation = 60.0f;

	/** Maximum instrumented detection range in cm (e.g. 30,000,000 cm = 300 km for AN/APG-77) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Range", meta = (ClampMin = "100000.0"))
	float MaxDetectionRange = 15000000.0f;

	/** Minimum detection range in cm (clutter rejection blind zone) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Range", meta = (ClampMin = "0.0"))
	float MinDetectionRange = 50000.0f;

	/** Currently selected display range scale in cm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Range", meta = (ClampMin = "1000.0"))
	float CurrentDisplayRange = 7400000.0f;

	UFUNCTION()
	void OnRep_CurrentDisplayRange();

	/** Selectable range scale presets in cm (e.g. 20nm, 40nm, 80nm, 160nm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Range")
	TArray<float> RangeScalePresets;

	/** Virtual mechanical antenna scan rate in degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.1", UIMin = "1.0", UIMax = "360.0", EditCondition = "ScanDrive == ERadarScanDrive::VirtualMechanical", EditConditionHides))
	float ScanRateDegreesPerSecond = 70.0f;

	/** Track update interval in seconds (how often track files are refreshed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.05", UIMin = "0.1", UIMax = "2.0"))
	float TrackUpdateInterval = 0.5f;

	/** Detection sample period; the mechanical and socket modes cover the sector crossed between samples. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Performance", meta = (ClampMin = "0.016", ClampMax = "1.0"))
	float ScanSampleInterval = 0.1f;

	/** Expected full antenna revolution in socket-driven mode. Set from the mesh animation rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Performance", meta = (ClampMin = "0.1", ClampMax = "120.0", EditCondition = "ScanDrive == ERadarScanDrive::SocketDriven", EditConditionHides))
	float SocketExpectedRevisitSeconds = 10.0f;

	/** Disable when all radar targets are registered with the combat subsystem; skips physics overlap discovery entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bEnablePhysicsCandidateDiscovery = true;

	/** How often physics overlaps discover actors absent from the combat registry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Performance", meta = (ClampMin = "0.1", ClampMax = "5.0", EditCondition = "bEnablePhysicsCandidateDiscovery", EditConditionHides))
	float CandidateDiscoveryInterval = 0.5f;

	/** Electronic beam visits per sample. PESA always uses one; AESA can use several. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Performance", meta = (ClampMin = "1", ClampMax = "32", EditCondition = "ScanDrive == ERadarScanDrive::AESA", EditConditionHides))
	int32 AESABeamsPerSample = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Track Management", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxTrackFiles = 64;

	/** Upper bound for expensive per-beam candidate evaluation; existing tracks take priority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Performance", meta = (ClampMin = "16", ClampMax = "4096"))
	int32 MaxCandidatesPerSample = 512;

	/** Minimum target Radar Cross Section (m²) detectable at MaxDetectionRange */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.001"))
	float MinimumDetectableRCS = 1.0f;

	/** Default RCS in m² assigned when a target has no 'RCS=X' tag (defaults to 99.99 m² as an unnatural indicator of a missing tag) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.0001"))
	float DefaultTargetRCS = 99.99f;

	/** Disable RCS tag parsing when every target should use DefaultTargetRCS. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bEnableTargetRCSTagParsing = true;

	/** If true, target heading/pitch aspect angle modifies effective RCS (e.g. beam broadside reflection vs nose-on) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bEnableAspectAngleRCS = true;

	/** RCS multiplier when target presents beam/broadside aspect (large specular reflection from fuselage and vertical stabilizers) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "1.0", ClampMax = "20.0", UIMin = "1.0", UIMax = "10.0", EditCondition = "bEnableAspectAngleRCS", EditConditionHides))
	float RCSAspectBeamMultiplier = 3.5f;

	/** RCS multiplier when target presents tail aspect (engine turbine cavity retro-reflections) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "1.0", ClampMax = "10.0", UIMin = "1.0", UIMax = "5.0", EditCondition = "bEnableAspectAngleRCS", EditConditionHides))
	float RCSAspectTailMultiplier = 1.8f;

	/** Receiver dynamic range in dB above detection sensitivity for normalized SignalStrength (default 40 dB: 0 dB = threshold, 40 dB = AGC saturation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "10.0", ClampMax = "80.0", UIMin = "20.0", UIMax = "60.0"))
	float ReceiverDynamicRangeDB = 40.0f;

	/** Minimum radial velocity in cm/s for Doppler track maintenance (notch filter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10000.0"))
	float NotchFilterVelocity = 3000.0f;

	/** Allows STT angle tracking to retain a notched target while range/Doppler data is degraded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (EditCondition = "RadarMode == ERadarOperatingMode::SingleTargetTrack", EditConditionHides))
	bool bSTTAngleTrackThroughNotch = true;

	/** If true, LOS ray trace checks are performed for look-down / terrain masking */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bEnableTerrainMasking = true;

	/** If true, queries all dynamic object types (Pawn, WorldDynamic, PhysicsBody, Vehicle) to prevent missing pawns with custom collision presets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (EditCondition = "bEnablePhysicsCandidateDiscovery", EditConditionHides))
	bool bQueryAllDynamicObjects = true;

	/** If true, also inspects component tags if the candidate actor itself does not have matching tags */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bSearchComponentTags = true;

	/** Radar antenna beam horizontal width in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "1.0", ClampMax = "45.0"))
	float BeamAzimuthWidth = 6.0f;

	/** Radar antenna beam vertical width in degrees (covers elevation bar spacing to eliminate blind gaps) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "1.0", ClampMax = "45.0"))
	float BeamElevationWidth = 10.0f;

	/** Collision channel for target detection overlaps (used when bQueryAllDynamicObjects is false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (EditCondition = "bEnablePhysicsCandidateDiscovery && !bQueryAllDynamicObjects", EditConditionHides))
	TEnumAsByte<ECollisionChannel> DetectionChannel = ECC_Pawn;

	/** Actor tags required for radar detection (if empty, all pawns are candidates) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	TArray<FName> DetectableActorTags;

	/** Target operational domains accepted in Air-to-Air modes (Search, TWS, ACM, STT). Defaults to [Air]. Remove or empty to accept all domains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Target Classification")
	TArray<ERadarTargetDomain> AirModeAllowedDomains = { ERadarTargetDomain::Air };

	/** Target operational domains accepted in Ground Mapping (GM) mode. Defaults to [Ground]. Remove or empty to accept all domains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Target Classification")
	TArray<ERadarTargetDomain> GroundMappingAllowedDomains = { ERadarTargetDomain::Ground };

	/** Target operational domains accepted in Sea Search (SS) mode. Defaults to [Sea]. Remove or empty to accept all domains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Target Classification")
	TArray<ERadarTargetDomain> SeaSearchAllowedDomains = { ERadarTargetDomain::Sea };

	/** If true, radar uses IGenericTeamAgentInterface to classify track IFF (Friendly/Hostile/Neutral). If false, all tracks remain Unknown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|IFF")
	bool bEnableIFF = true;

	/** How unclassified contacts are treated when the target actor does not implement IGenericTeamAgentInterface */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|IFF", meta = (EditCondition = "bEnableIFF", EditConditionHides))
	EIFFUnknownAttitude UnknownContactAttitude = EIFFUnknownAttitude::Neutral;

	/** Team ID for this aircraft / SAM platform (0-254 = Factions, 255 = NoTeam/Neutral). Acts as transponder when platform has no separate IFF component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_TeamID, Category = "Radar|IFF")
	uint8 TeamID = 1;

	/** Squawk code for civilian/ATC or military Mode 3/A transponder squawk */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_SquawkCode, Category = "Radar|IFF", meta = (ClampMin = "0", ClampMax = "7777"))
	int32 SquawkCode = 1200;

	/** Broadcast when radar platform's team ID changes */
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events")
	FOnCombatTeamChangedSignature OnTeamChanged;

	UFUNCTION()
	void OnRep_TeamID();

	UFUNCTION()
	void OnRep_SquawkCode();

	/** Sets the radar platform's team ID (Server/Authoritative). Updates all replicated clients. */
	UFUNCTION(BlueprintCallable, Category = "Radar|IFF")
	void SetTeamID(uint8 NewTeamID);

	/** Returns the assigned team ID */
	UFUNCTION(BlueprintPure, Category = "Radar|IFF")
	uint8 GetTeamID() const { return TeamID; }

	/** Sets the radar platform's squawk code (Server/Authoritative). */
	UFUNCTION(BlueprintCallable, Category = "Radar|IFF")
	void SetSquawkCode(int32 NewSquawkCode);

	/** Returns the current squawk code */
	UFUNCTION(BlueprintPure, Category = "Radar|IFF")
	int32 GetSquawkCode() const { return SquawkCode; }

	// ~Begin IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	// ~End IGenericTeamAgentInterface

	/** Target operational domains accepted in Spotlight SAR mode. Defaults to [Ground, Sea]. Remove or empty to accept all domains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Target Classification")
	TArray<ERadarTargetDomain> SpotlightAllowedDomains = { ERadarTargetDomain::Ground, ERadarTargetDomain::Sea };

	/** Maximum number of simultaneous TWS track files */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management", meta = (ClampMin = "1", ClampMax = "50"))
	int32 MaxSimultaneousTWSTracks = 10;

	/** Time in seconds before a track file is dropped due to no radar return */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management", meta = (ClampMin = "1.0"))
	float TrackDropTimeout = 8.0f;

	/** ACM auto-lock range in cm (targets within this range are auto-locked in ACM mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management")
	float ACMAutoLockRange = 1800000.0f;

	/** ACM boresight cone half-angle in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management", meta = (ClampMin = "5.0", ClampMax = "45.0", EditCondition = "ACMSubMode != ERadarACMSubMode::VerticalScan", EditConditionHides))
	float ACMBoresightConeAngle = 10.0f;

	/** Total azimuth width in degrees of the ACM Vertical Scan swath (default 10.0° -> ±5° from aircraft centerline) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management", meta = (ClampMin = "2.0", ClampMax = "30.0", UIMin = "2.0", UIMax = "20.0", EditCondition = "ACMSubMode == ERadarACMSubMode::VerticalScan", EditConditionHides))
	float ACMVerticalScanAzimuthWidth = 10.0f;

	/** Minimum elevation limit in degrees for ACM Vertical Scan (default -10.0°, slightly below HUD waterline) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management", meta = (ClampMin = "-30.0", ClampMax = "10.0", UIMin = "-20.0", UIMax = "5.0", EditCondition = "ACMSubMode == ERadarACMSubMode::VerticalScan", EditConditionHides))
	float ACMVerticalScanMinElevation = -10.0f;

	/** Maximum elevation limit in degrees for ACM Vertical Scan (default +60.0°, high canopy lift vector) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management", meta = (ClampMin = "15.0", ClampMax = "85.0", UIMin = "30.0", UIMax = "75.0", EditCondition = "ACMSubMode == ERadarACMSubMode::VerticalScan", EditConditionHides))
	float ACMVerticalScanMaxElevation = 60.0f;

	/** Radius of the high-resolution SAR Spotlight ground patch in cm (default 200,000 cm = 2 km) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "50000.0", ClampMax = "1000000.0", UIMin = "50000.0", UIMax = "500000.0"))
	float SpotlightPatchRadius = 200000.0f;

	/** Dwell duration in seconds required to synthesize full cross-range SAR image resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "0.5", ClampMax = "10.0", UIMin = "1.0", UIMax = "5.0"))
	float SpotlightDwellDuration = 2.5f;

	/** Minimum squint angle in degrees relative to aircraft velocity (below this angle, Doppler gradient is zero / Doppler blind cone) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "2.0", ClampMax = "30.0"))
	float SpotlightMinSquintAngle = 10.0f;

	/** Maximum squint angle in degrees for SAR image synthesis */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "30.0", ClampMax = "85.0"))
	float SpotlightMaxSquintAngle = 75.0f;

	/** If true, automatically traces forward/downward along radar waterline to intersect terrain if no ground point was manually designated */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR")
	bool bSpotlightAutoGroundIntersect = true;

	/** Default slant range in cm used for initial ground intersect projection if terrain trace misses */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "500000.0", EditCondition = "bSpotlightAutoGroundIntersect", EditConditionHides))
	float SpotlightDefaultSlantRange = 2500000.0f;

	/** Default downward pitch angle in degrees from radar centerline for auto-intersect trace (default -12°) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "-45.0", ClampMax = "0.0", EditCondition = "bSpotlightAutoGroundIntersect", EditConditionHides))
	float SpotlightDefaultPitchAngle = -12.0f;

	/** Ground velocity threshold in cm/s to classify a contact as moving (GMTI) vs stationary structure (default 150 cm/s ≈ 3 knots) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "10.0"))
	float GMTIVelocityThreshold = 150.0f;

	/** Master switch for debug visualization and HUD telemetry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug")
	bool bEnableDebugTraces = false;

	/** If true, 3D debug visualizations are only rendered if owner is locally player-controlled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces", EditConditionHides))
	bool bDebugOnlyPlayerControlled = false;

	/** If true, renders the 3D scan volume wireframe frustum and elevation bars */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces", EditConditionHides))
	bool bDrawScanVolume = true;

	/** If true, renders the 3D antenna beam boresight ray and scanning cone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces", EditConditionHides))
	bool bDrawAntennaBeam = true;

	/** If true, renders 3D track symbology (diamonds, STT reticles, velocity vectors) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces", EditConditionHides))
	bool bDrawTrackSymbology = true;

	/** If true, renders on-screen live telemetry diagnostics table */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces", EditConditionHides))
	bool bEnableDiagnosticHUD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces && bEnableTargetCursor", EditConditionHides))
	bool bDebugCursor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces", EditConditionHides))
	bool bDebugCandidateRejections = false;

	/** Labels throttled overlap discovery batches and their raw hit count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces && bEnablePhysicsCandidateDiscovery", EditConditionHides))
	bool bDebugPhysicsDiscovery = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces", EditConditionHides))
	bool bDebugReferenceAxes = false;

	// -----------------------------------------------------
	// Radar Altimeter (RALT / AGL) Subsystem
	// -----------------------------------------------------

	/** If true, the radar component operates a radar altimeter subsystem measuring height Above Ground Level (AGL) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter")
	bool bEnableRadarAltimeter = true;

	/** Optional socket name on the aircraft mesh for the radar altimeter antenna. If None, falls back to RadarSocketName or actor root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", EditConditionHides))
	FName RadarAltimeterSocketName = NAME_None;

	/** Maximum operational altitude for the radar altimeter in cm (e.g. 152,400 cm = 5,000 ft). Above this ceiling, the altimeter flags invalid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", EditConditionHides, ClampMin = "1000.0"))
	float MaxRadarAltitude = 152400.0f;

	/** Maximum aircraft attitude tilt angle (pitch or bank from level) in degrees before altimeter beam breaks ground lock (default 50°) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", EditConditionHides, ClampMin = "10.0", ClampMax = "89.0"))
	float MaxAltimeterAttitudeAngle = 50.0f;

	/** If true, uses a multi-ray conical sweep to detect closest terrain obstacle/slope (first-return); if false, uses a single vertical nadir trace */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", EditConditionHides))
	bool bAltimeterConicalSampling = true;

	/** Full conical beamwidth of the radar altimeter in degrees (e.g. 45°). Used for first-return terrain proximity detection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter && bAltimeterConicalSampling", EditConditionHides, ClampMin = "10.0", ClampMax = "90.0"))
	float AltimeterBeamwidthDegrees = 45.0f;

	/** Smoothing speed for the radar altimeter tracking loop (higher = more responsive, lower = smoother) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", EditConditionHides, ClampMin = "1.0", ClampMax = "50.0"))
	float AltimeterSmoothingSpeed = 15.0f;

	/** How often the radar altimeter updates in seconds (e.g. 0.033 = ~30 Hz, 0.0 = every frame) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", UIMax = "0.2"))
	float AltimeterUpdateInterval = 0.033f;

	/** Sets the radar operating mode */
	UFUNCTION(BlueprintCallable, Category = "Radar|Control")
	void SetRadarMode(ERadarOperatingMode NewMode);

	/** Returns a request ID for OnRadarCommandResult, or INDEX_NONE when no local operator link exists. */
	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestSetRadarMode(ERadarOperatingMode NewMode, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestSetDisplayRange(float NewRangeCm, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestLockTrack(int32 TrackID, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestBugTrack(int32 TrackID, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestBreakLock(APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Display Projection|Target Cursor")
	int32 RequestDesignateCursor(APlayerController* RequestingController);

	/** Designate the symbol beneath the cursor in the radar's active display view. */
	UFUNCTION(BlueprintCallable, Category = "Display Projection|Target Cursor")
	int32 RequestDesignateUnderDisplayCursor(APlayerController* RequestingController);

	/** Shared operator view. PPI offset is km right/forward; B-scope offset is degrees/km. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display Projection")
	FRadarDisplayView DisplayView;

	UFUNCTION(BlueprintPure, Category = "Display Projection")
	FRadarDisplayView GetDisplayView() const { return DisplayView; }

	UFUNCTION(BlueprintPure, Category = "Display Projection")
	int32 GetDisplayViewRevision() const { return DisplayViewRevision; }

	/** Changes the active geometry's window. RequestingController must be the active operator;
	 * check OnRadarCommandResult for the server's success result. */
	UFUNCTION(BlueprintCallable, Category = "Display Projection")
	int32 RequestSetDisplayWindow(float ZoomFactor,
		FVector2D ViewCenterOffset, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Display Projection")
	int32 RequestSetDisplayGeometry(ERadarDisplayGeometry Geometry, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Display Projection")
	int32 RequestSetDisplayHeadingUp(bool bHeadingUp, APlayerController* RequestingController);

	/** Center-anchored canvas translation: (0,0) is the MFD center. Use for waypoints,
	 * track LastKnownPosition, and FRadarCursorState::WorldLocation. */
	UFUNCTION(BlueprintPure, Category = "Display Projection")
	bool ProjectWorldToDisplay(const FVector& WorldLocation, FVector2D WidgetSize,
		FVector2D& OutWidgetPosition) const;

	/** WidgetPosition is center-relative, matching ProjectWorldToDisplay's output. */
	UFUNCTION(BlueprintCallable, Category = "Display Projection|Target Cursor")
	int32 RequestSetCursorFromDisplayPosition(FVector2D WidgetPosition, FVector2D WidgetSize,
		APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Display Projection|Target Cursor")
	int32 RequestSetCursorFromWidgetPosition(const FVector2D& WidgetPosition,
		const FRadarDisplayProjection& Projection, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestSetACMSubMode(ERadarACMSubMode NewSubMode, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestSetScanVolume(float AzimuthWidth, float ElevationHeight, int32 Bars,
		APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestOffsetScanCenter(float AzimuthDelta, float ElevationDelta,
		APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestApplyScanSizePreset(ERadarScanSize Preset, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestClearBugTrack(APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestDesignateSpotlightPoint(const FVector& WorldLocation, APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestClearSpotlightTarget(APlayerController* RequestingController);

	UFUNCTION(BlueprintCallable, Category = "Radar|Requests")
	int32 RequestSetHelmetLookDirection(const FVector& WorldDirection, APlayerController* RequestingController);

	/** Axis input uses an unreliable 20 Hz operator channel and times out automatically. */
	UFUNCTION(BlueprintCallable, Category = "Display Projection|Target Cursor")
	bool RequestMoveTDCCursor(float XAxis, float YAxis, APlayerController* RequestingController);

	/** Returns the current radar mode */
	UFUNCTION(BlueprintPure, Category = "Radar|Control")
	FORCEINLINE ERadarOperatingMode GetRadarMode() const { return RadarMode; }

	/** Returns the current ACM sub-mode */
	UFUNCTION(BlueprintPure, Category = "Radar|Control")
	FORCEINLINE ERadarACMSubMode GetACMSubMode() const { return ACMSubMode; }

	/** Sets the socket name used as the radar antenna origin and boresight axis */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Configuration")
	void SetRadarSocketName(FName InSocketName);

	/** Returns the socket name used as the radar antenna origin */
	UFUNCTION(BlueprintPure, Category = "Radar|Configuration")
	FORCEINLINE FName GetRadarSocketName() const { return RadarSocketName; }

	/** Returns the world-space location of the radar antenna (uses RadarSocketName if valid, otherwise falls back to root (0, 0, 0) / actor origin) */
	UFUNCTION(BlueprintPure, Category = "Radar|Source")
	FVector GetRadarLocation() const;

	/** Returns the world-space rotation/orientation of the radar antenna (uses RadarSocketName if valid, otherwise falls back to root actor rotation) */
	UFUNCTION(BlueprintPure, Category = "Radar|Source")
	FRotator GetRadarRotation() const;

	/** Returns both world-space location and rotation of the radar antenna source */
	UFUNCTION(BlueprintPure, Category = "Radar|Source")
	void GetRadarSourceTransform(FVector& OutLocation, FRotator& OutRotation) const;

	/** Stable platform frame for track bearing and display projection. */
	UFUNCTION(BlueprintPure, Category = "Radar|Source")
	void GetRadarReferenceTransform(FVector& OutLocation, FRotator& OutRotation) const;

	UFUNCTION(BlueprintPure, Category = "Radar|Display")
	FRadarDisplayProjection MakeDisplayProjection(const FVector2D& WidgetTopLeft, const FVector2D& WidgetSize,
		ERadarDisplayGeometry Geometry = ERadarDisplayGeometry::BScope, bool bHeadingUp = true) const;

	/** Returns true if the radar is actively emitting RF energy (not Off, not Standby) */
	UFUNCTION(BlueprintPure, Category = "Radar|Control")
	bool IsRadarEmitting() const;

	/** Sets the ACM sub-mode and switches to ACM mode if not already */
	UFUNCTION(BlueprintCallable, Category = "Radar|Control")
	void SetACMSubMode(ERadarACMSubMode NewSubMode);

	/**
	 * Evaluates whether a direction vector (in local radar component coordinates) falls within the active ACM sub-mode acquisition volume.
	 * @param LocalDirection Direction vector in local radar component coordinates (X = Forward, Y = Right, Z = Up).
	 * @param SubMode The active ACM sub-mode being evaluated.
	 * @return True if within the sub-mode acquisition envelope.
	 */
	bool IsDirectionInACMVolume(const FVector& LocalDirection, ERadarACMSubMode SubMode) const;

	/** Sets the scan volume parameters */
	UFUNCTION(BlueprintCallable, Category = "Radar|Scan")
	void SetScanVolume(float Azimuth, float Elevation, int32 Bars);

	/** Applies a scan size preset */
	UFUNCTION(BlueprintCallable, Category = "Radar|Scan")
	void ApplyScanSizePreset(ERadarScanSize Preset);

	/** Offsets the scan center (TDC cursor slew) */
	UFUNCTION(BlueprintCallable, Category = "Radar|Scan")
	void OffsetScanCenter(float AzDelta, float ElDelta);

	/** Cycles the display range scale up or down */
	UFUNCTION(BlueprintCallable, Category = "Radar|Range")
	void CycleRangeScale(bool bIncrease);

	/** Sets the display range scale directly in cm */
	UFUNCTION(BlueprintCallable, Category = "Radar|Range")
	void SetRangeScale(float NewRange);

	/**
	 * Returns the current radar altitude Above Ground Level (AGL) in centimeters.
	 *
	 * Performs a realistic first-return RF altimeter measurement accounting for terrain contours,
	 * antenna mounting location, attitude cutoff limits (bank/pitch), and maximum operational ceiling.
	 *
	 * @param OutAltitudeCm The measured height above terrain in Unreal centimeters (cm).
	 * @return True if a valid radar altimeter return is available (within operational ceiling and attitude limits); false otherwise.
	 */
	UFUNCTION(BlueprintPure, Category = "Radar|Altimeter")
	bool GetRadarAltitude(float& OutAltitudeCm) const;

	/** Sets the socket name on the aircraft mesh used as the radar altimeter antenna source */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Altimeter")
	void SetRadarAltimeterSocketName(FName InSocketName);

	/** Returns the socket name used as the radar altimeter antenna source */
	UFUNCTION(BlueprintPure, Category = "Radar|Altimeter")
	FORCEINLINE FName GetRadarAltimeterSocketName() const { return RadarAltimeterSocketName; }

	/** Returns both world-space location and rotation of the radar altimeter antenna source */
	UFUNCTION(BlueprintPure, Category = "Radar|Altimeter")
	void GetRadarAltimeterTransform(FVector& OutLocation, FRotator& OutRotation) const;

	/** Returns the current scan antenna azimuth position in degrees (for HUD B-scope rendering) */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	FORCEINLINE float GetCurrentScanAzimuth() const { return CurrentScanAzimuth; }

	/** Returns the current scan antenna elevation bar index */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	FORCEINLINE int32 GetCurrentScanBar() const { return CurrentScanBar; }

	/** Returns true if the antenna is currently sweeping to the right */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	FORCEINLINE bool IsScanningRight() const { return bScanningRight; }

	/** Last authoritative scan progress sample, also available after a late UI bind. */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	void GetScanProgress(int32& OutBar, float& OutBarLevel, float& OutSweepLevel, bool& bOutScanningRight) const;

	/** Returns the normalized scan position as (Azimuth%, Elevation%) for HUD display */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	FVector2D GetCurrentScanPosition() const;

	/** Returns true if the given actor is within the current scan volume */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	bool IsTargetInScanVolume(AActor* Target) const;

	/** Calculates the maximum detection range for a target with the given RCS in m² */
	float CalculateDetectionRange(float TargetRCS) const;

	/**
	 * Resolves the Radar Cross Section (in m²) for a candidate actor by parsing 'RCS=X' or 'RCS:X' tags.
	 * Inspects Candidate Actor tags, and if bSearchComponentTags is true, inspects Component tags.
	 * @param Candidate The actor being scanned.
	 * @param bOutFoundTag Set to true if an explicit 'RCS=X' tag was found; false if defaulting to the unnatural fallback.
	 * @param OutParsedTag The raw tag string that matched (or "NONE").
	 * @return Parsed RCS in m², or DefaultTargetRCS (unnatural number) if not found.
	 */
	float ResolveTargetRCS(const AActor* Candidate, bool& bOutFoundTag, FString& OutParsedTag) const;

	/**
	 * Calculates the effective target RCS in m² taking into account aspect angle (relative orientation).
	 * Computes target aspect without expensive trigonometric functions using vector dot products.
	 * @param BaseRCS The baseline target RCS (e.g. from tags or default).
	 * @param TargetActor The target actor (used for heading/forward orientation).
	 * @param TargetToRadar Normalized direction vector pointing from target towards the radar.
	 * @return Aspect-modified effective RCS in m².
	 */
	float CalculateEffectiveRCS(float BaseRCS, const AActor* TargetActor, const FVector& TargetToRadar) const;

	/**
	 * Calculates the one-way antenna beam gain factor (0.0 to 1.0) for a target direction.
	 * Accounts for mechanical beam Gaussian roll-off, AESA array scan loss, or STT boresight lock.
	 * @param TargetBearing Local bearing from radar forward in degrees.
	 * @param TargetElevation Local elevation from radar waterline in degrees.
	 * @param LocalTargetDir Normalized local direction vector to target in radar antenna space.
	 * @return Antenna beam gain factor (0.01 to 1.0).
	 */
	float CalculateAntennaBeamGain(float TargetBearing, float TargetElevation, const FVector& LocalTargetDir) const;

	/** Designates a world coordinate for Spotlight SAR ground-stared imaging */
	UFUNCTION(BlueprintCallable, Category = "Radar|Spotlight")
	void DesignateSpotlightPoint(const FVector& WorldLocation);

	/** Designates a specific actor on the ground for Spotlight SAR slaved tracking */
	UFUNCTION(BlueprintCallable, Category = "Radar|Spotlight")
	void DesignateSpotlightActor(AActor* TargetActor);

	/** Clears current Spotlight target coordinate */
	UFUNCTION(BlueprintCallable, Category = "Radar|Spotlight")
	void ClearSpotlightTarget();

	/** Returns the currently designated Spotlight ground location */
	UFUNCTION(BlueprintPure, Category = "Radar|Spotlight")
	FORCEINLINE FVector GetSpotlightTargetLocation() const { return SpotlightTargetLocation; }

	/** Returns the current SAR dwell synthesis progress (0.0 to 1.0) */
	UFUNCTION(BlueprintPure, Category = "Radar|Spotlight")
	FORCEINLINE float GetSpotlightDwellProgress() const { return SpotlightDwellProgress; }

	/** Returns the current squint angle in degrees to the spotlight ground target */
	UFUNCTION(BlueprintPure, Category = "Radar|Spotlight")
	FORCEINLINE float GetSpotlightSquintAngle() const { return SpotlightSquintAngle; }

	/** Returns true if Spotlight mode is active with an active ground lock */
	UFUNCTION(BlueprintPure, Category = "Radar|Spotlight")
	FORCEINLINE bool IsSpotlightActive() const { return RadarMode == ERadarOperatingMode::Spotlight && bHasSpotlightPoint; }

	/** Sets the helmet or pilot camera look direction in world coordinates for HelmetCue ACM mode */
	UFUNCTION(BlueprintCallable, Category = "Radar|Helmet Cue")
	void SetHelmetLookDirection(const FVector& InWorldDirection);

	/** Returns the current helmet look direction in world coordinates */
	UFUNCTION(BlueprintPure, Category = "Radar|Helmet Cue")
	FORCEINLINE FVector GetHelmetLookDirection() const { return HelmetLookDirection; }

	/** Resolves the operational domain (Air, Ground, Sea) of a candidate actor based on its tags. Defaults to Air if untagged. */
	ERadarTargetDomain ResolveCandidateDomain(const AActor* Candidate) const;

	/** Returns true if the given target domain is allowed in the specified radar operating mode */
	bool IsDomainAllowedForMode(ERadarTargetDomain Domain, ERadarOperatingMode Mode) const;

	/**
	 * Calculates normalized radar return signal strength (0.0 to 1.0) using two-way radar equation SNR.
	 * Normalized over ReceiverDynamicRangeDB (e.g. 40 dB: 0 dB = detection threshold, 40 dB = saturation).
	 * @param Range Slant range to target in cm.
	 * @param EffectiveRCS Effective target RCS in m² (aspect-modified).
	 * @param BeamGain One-way antenna beam gain (0.0 to 1.0).
	 * @return Normalized signal strength [0.0, 1.0].
	 */
	float CalculateSignalStrength(float Range, float EffectiveRCS, float BeamGain) const;

	/** Checks if a target is terrain-masked (LOS blocked by ground geometry) */
	bool IsTerrainMasked(const FVector& RadarPosition, const FVector& TargetPosition, const AActor* TargetActor = nullptr, FHitResult* OutHit = nullptr) const;

	/** Commands STT lock on a specific track file (transitions radar to STT mode) */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock", meta = (DeprecatedFunction, DeprecationMessage = "Use RequestLockTrack and OnRadarCommandResult for multiplayer."))
	bool CommandLock(int32 TrackID);

	/** Commands STT lock on a specific target actor by searching active tracks */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Lock")
	bool CommandLockActor(AActor* TargetActor);

	/**
	 * Acquires or locks a specific actor immediately.
	 * If the actor is already in tracks, locks or bugs it.
	 * If not yet tracked but within radar parameters, evaluates and creates a track immediately.
	 *
	 * @param TargetActor The actor to acquire
	 * @param bForceSTT If true, transitions to STT mode; if false, bugs in TWS or acquires in Search
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Lock")
	bool AcquireOrLockActor(AActor* TargetActor, bool bForceSTT = false);

	/** Breaks the current STT lock and returns to the previous scan mode */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock")
	void BreakLock();

	/** Bugs/designates a TWS track for priority tracking and weapon cueing (does not change to STT) */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock", meta = (DeprecatedFunction, DeprecationMessage = "Use RequestBugTrack and OnRadarCommandResult for multiplayer."))
	bool CommandBugTrack(int32 TrackID);

	/** Clears the TWS bug designation */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock")
	void ClearBugTrack();

	/** Returns true if the radar is in STT mode with an active lock */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	bool IsSTTLocked() const;

	/** Returns the actor currently locked in STT mode (or nullptr) */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	AActor* GetSTTLockedActor() const;

	/** Returns the track ID of the STT locked track (-1 if none) */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	FORCEINLINE int32 GetSTTLockedTrackID() const { return STTLockedTrackID; }

	/** Returns the track ID of the TWS bugged track (-1 if none) */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	FORCEINLINE int32 GetBuggedTrackID() const { return BuggedTrackID; }

	/** Returns the currently selected track (either STT locked or bugged in TWS) */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	bool GetSelectedTrack(FRadarTrack& OutTrack) const;

	/** Returns the actor of the currently selected track (STT locked or bugged, or nullptr) */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	AActor* GetSelectedTargetActor() const;

	// -----------------------------------------------------
	// Continuous Wave (CW) / Missile Guidance Illumination
	// -----------------------------------------------------

	/**
	 * Registers an in-flight missile requiring parent radar illumination (SARH, TVM, or Command Guidance).
	 * Tracks launched supported missiles for RWR reporting. Registration does not create illumination.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Guidance")
	void RegisterGuidingMissile(URadarMissileGuidanceComponent* Missile);

	/**
	 * Unregisters a missile that has finished its flight (hit, detonated, or lost lock).
	 * If no other missiles require illumination, CW illumination ceases.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Guidance")
	void UnregisterGuidingMissile(URadarMissileGuidanceComponent* Missile);

	/** Returns true if this radar is actively transmitting Continuous Wave (CW) illumination or guidance bursts towards TargetActor (or any target if TargetActor is null) */
	UFUNCTION(BlueprintPure, Category = "Radar|Guidance")
	bool IsContinuousWaveIlluminating(const AActor* TargetActor = nullptr) const;

	/** Returns the current continuous wave (CW) illuminated target actor (if active) */
	UFUNCTION(BlueprintPure, Category = "Radar|Guidance")
	AActor* GetContinuousWaveTarget() const { return ManualCWTargetActor.Get(); }

	/**
	 * Manually controls Continuous Wave (CW) illumination towards a target actor.
	 * Used by SAM batteries, ships, AI controllers, or mission scripts to engage missile guidance modes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Guidance")
	void SetContinuousWaveIllumination(AActor* TargetActor, bool bEnable);

	/** Returns all currently active guided missiles relying on this radar */
	UFUNCTION(BlueprintPure, Category = "Radar|Guidance")
	TArray<URadarMissileGuidanceComponent*> GetActiveGuidingMissiles() const;

	/** Launch ownership is independent of whichever radar currently supplies guidance. */
	void RegisterLaunchedRadarMissile(URadarMissileGuidanceComponent* Missile);
	void UnregisterLaunchedRadarMissile(URadarMissileGuidanceComponent* Missile);
	/** Current launched radar missiles. Clients receive this through the authorized operator snapshot. */
	UFUNCTION(BlueprintPure, Category = "Radar|Launched Missiles")
	void GetLaunchedRadarMissiles(TArray<FRadarLaunchedMissileStatus>& OutMissiles) const;
	/** Cadence shared by operator display state and launched missile status. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Multiplayer", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float OperatorSnapshotIntervalSeconds = 0.2f;

	/** Completely disables TDC input, projection, designation, events, and debug for radars without a cursor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display Projection|Target Cursor")
	bool bEnableTargetCursor = true;

	UFUNCTION(BlueprintPure, Category = "Display Projection|Target Cursor")
	bool IsTargetCursorEnabled() const { return bEnableTargetCursor; }

	/** Current TDC (Target Designator Control) cursor azimuth offset in degrees relative to antenna scan center */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Display Projection|Target Cursor")
	float TDCCursorAzimuth = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Display Projection|Target Cursor")
	float TDCCursorElevation = 0.0f;

	/** Current TDC cursor range position in cm */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Display Projection|Target Cursor")
	float TDCCursorRange = 3704000.0f; // Default 20nm

	/** B-scope horizontal speed, in degrees per second at zoom 1. Zoom scales the world-space movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection|Target Cursor", meta = (ClampMin = "1.0", UIMin = "5.0", UIMax = "120.0", EditCondition = "bEnableTargetCursor", EditConditionHides))
	float CursorAzimuthSpeed = 40.0f;

	/** B-scope vertical speed as a fraction of the full range per second at zoom 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection|Target Cursor", meta = (ClampMin = "0.05", ClampMax = "2.0", UIMin = "0.1", UIMax = "1.0", EditCondition = "bEnableTargetCursor", EditConditionHides))
	float CursorRangeSpeedFraction = 0.4f;

	/** Fraction of the visible PPI MFD width/height crossed per second at full input. B-scope uses the existing azimuth and range speed settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection|Target Cursor", meta = (ClampMin = "0.05", ClampMax = "2.0", EditCondition = "bEnableTargetCursor", EditConditionHides))
	float CursorDisplaySpeedFraction = 0.4f;

	/** Selection radius in fractions of the visible display width/height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection|Target Cursor", meta = (ClampMin = "0.005", ClampMax = "0.08", EditCondition = "bEnableTargetCursor", EditConditionHides))
	float CursorSelectionRadiusFraction = 0.04f;

	/**
	 * Moves the radar target designator (TDC) cursor on the display using 2D axis inputs.
	 * Automatically calculates delta time from the world.
	 *
	 * @param XAxis Horizontal movement input (-1.0 to +1.0) in the active MFD view.
	 * @param YAxis Vertical movement input (-1.0 to +1.0) in the active MFD view.
	 */
	UFUNCTION(BlueprintCallable, Category = "Display Projection|Target Cursor", meta = (DisplayName = "Move TDC Cursor", DeprecatedFunction, DeprecationMessage = "Use RequestMoveTDCCursor with the owning player controller for UI input."))
	void MoveTDCCursor(float XAxis, float YAxis);

	/** Direct mouse/touch placement. Returns false when the point is outside the display. */
	UFUNCTION(BlueprintCallable, Category = "Display Projection|Target Cursor", meta = (DeprecatedFunction, DeprecationMessage = "Use RequestSetCursorFromDisplayPosition and OnRadarCommandResult for UI input."))
	bool SetTDCCursorFromWidgetPosition(const FVector2D& WidgetPosition, const FRadarDisplayProjection& Projection);

	UFUNCTION(BlueprintPure, Category = "Display Projection|Target Cursor")
	FRadarCursorState GetTDCCursorState() const;

	UFUNCTION(BlueprintPure, Category = "Display Projection|Target Cursor", meta = (DeprecatedFunction, DeprecationMessage = "Use TryGetTDCCursorWorldLocation to handle a disabled cursor."))
	FVector GetTDCCursorWorldLocation() const;

	/** Returns false and a zero point when the TDC is disabled. */
	UFUNCTION(BlueprintPure, Category = "Display Projection|Target Cursor")
	bool TryGetTDCCursorWorldLocation(FVector& OutWorldLocation) const;

	/** Returns normalized (X: 0..1, Y: 0..1) TDC cursor coordinates for MFD screen space rendering */
	UFUNCTION(BlueprintPure, Category = "Display Projection|Target Cursor", meta = (DeprecatedFunction, DeprecationMessage = "Project GetTDCCursorState.WorldLocation with ProjectWorldToDisplay."))
	FVector2D GetTDCCursorScreenPosition() const;

	/** Designates track under cursor (Bugs an unbugged track, or locks a bugged track to STT; traditionally Stick TMS Up / TDC Depress) */
	UFUNCTION(BlueprintCallable, Category = "Display Projection|Target Cursor", meta = (DeprecatedFunction, DeprecationMessage = "Use RequestDesignateUnderDisplayCursor."))
	bool DesignateTrackUnderCursor(float AzimuthGateDegrees = 6.0f, float RangeGatePercent = 0.15f);

	/** Selection in normalized display space, with the same projection used by tracks and cursor. */
	UFUNCTION(BlueprintCallable, Category = "Display Projection|Target Cursor")
	bool DesignateTrackUnderCursorInDisplay(ERadarDisplayGeometry Geometry, float NormalizedGate = 0.04f);

	/** Last measured position plus bounded dead-reckoning, without consulting the live actor. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	bool GetTrackDisplayWorldPosition(int32 TrackID, FVector& OutWorldPosition) const;

	/** Server-only authorization for crew on this radar platform. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Multiplayer")
	bool GrantRadarAccess(APlayerController* Controller);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Multiplayer")
	void RevokeRadarAccess(APlayerController* Controller);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Multiplayer")
	bool SetActiveRadarOperator(APlayerController* Controller);

	/** Local owner-only channel, or null when the local player lacks radar access. */
	UFUNCTION(BlueprintPure, Category = "Radar|Multiplayer")
	ARadarOperatorLink* GetLocalOperatorLink() const { return LocalOperatorLink.Get(); }

	// Called by the owner-only link after a coherent replicated snapshot is available.
	void RegisterLocalOperatorLink(ARadarOperatorLink* Link);
	void UnregisterLocalOperatorLink(ARadarOperatorLink* Link);
	void ApplyOperatorSnapshot(const FRadarOperatorSnapshot& Snapshot);
	void ApplyOperatorEvent(const FRadarOperatorEvent& Event);
	bool IsAuthorizedOperatorLink(const ARadarOperatorLink* Link) const;
	bool ExecuteOperatorCommand(ERadarCommandType Command, int32 IntValue, float ValueA, float ValueB,
		const FVector& WorldValue, const FRadarCursorState& Cursor, ERadarDisplayGeometry Geometry,
		int32& OutTrackID, int32 ViewRevision = -1);

	/** Clears target designation or breaks active STT lock (traditionally Stick TMS Down / Undesignate) */
	UFUNCTION(BlueprintCallable, Category = "Radar|Target Selection")
	void UndesignateTarget();

	/** Cycles target designation sequentially between active tracks (traditionally Stick TMS Right / Target Step) */
	UFUNCTION(BlueprintCallable, Category = "Radar|Target Selection")
	bool CycleTargetDesignation(bool bForward = true);

	/** Converts a track's position to normalized B-scope MFD screen coordinates: X (-1..+1), Y (0..1) */
	UFUNCTION(BlueprintPure, Category = "Display Projection", meta = (DeprecatedFunction, DeprecationMessage = "Project FRadarTrack.LastKnownPosition with ProjectWorldToDisplay."))
	bool GetTrackBScopePosition(int32 TrackID, FVector2D& OutScreenPos) const;

	/** Computes target aspect angle in degrees (-180..+180, 0 = pure tail-on, 180 = pure head-on) for display heading vectors */
	UFUNCTION(BlueprintPure, Category = "Radar|Display")
	float GetTrackAspectAngle(int32 TrackID) const;

	/**
	 * Calculates the horizontal ground-track compass heading (0-360 degrees) from a velocity vector.
	 * If horizontal speed is below MinSpeedCmPerSec, returns FallbackHeading to prevent noise on stationary targets.
	 *
	 * @param InVelocity Velocity vector in world coordinates
	 * @param FallbackHeading Heading in degrees (0-360) to maintain if target is stationary
	 * @param MinSpeedCmPerSec Minimum horizontal speed threshold in cm/s (default 100 cm/s = 1 m/s)
	 * @return Compass heading in degrees [0.0, 360.0)
	 */
	UFUNCTION(BlueprintPure, Category = "Radar|Kinematics")
	static float CalculateHeadingFromVelocity(const FVector& InVelocity, float FallbackHeading = 0.0f, float MinSpeedCmPerSec = 100.0f);

	/** Returns all active radar tracks */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	const TArray<FRadarTrack>& GetAllTracks() const { return Tracks; }

	/** Returns tracks filtered by status */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	void GetTracksByStatus(ERadarTrackStatus InStatus, TArray<FRadarTrack>& OutTracks) const;

	/** Retrieves a specific track by ID */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	bool GetTrackByID(int32 TrackID, FRadarTrack& OutTrack) const;

	/** Retrieves a track associated with a specific target actor */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	bool GetTrackByActor(const AActor* TargetActor, FRadarTrack& OutTrack) const;

	/** Returns the actor of the closest contact */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	AActor* GetClosestContact() const;

	/** Returns the bearing to a specific track in degrees */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	float GetBearingToTrack(int32 TrackID) const;

	/** Returns the range to a specific track in cm */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	float GetRangeToTrack(int32 TrackID) const;

	/** Returns the closure rate to a specific track in cm/s */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	float GetClosureRateToTrack(int32 TrackID) const;

	/** Returns the total number of active tracks */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	FORCEINLINE int32 GetContactCount() const { return Tracks.Num(); }

	/** Returns the track that is the highest threat (closest, fastest closing) */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	bool GetHighestThreatTrack(FRadarTrack& OutTrack) const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Scan & Detection
	/** Advances antenna azimuth, sweeps elevation bars, and optionally broadcasts HUD progress and cycle events */
	void AdvanceAntennaSweep(float DeltaTime, bool bBroadcastEvents = true);
	void EmitScanProgress(int32 Bar, float BarLevel, float SweepLevel, bool bMovingRight);

	/** Gathers candidate actors within Range of Origin using spatial subsystem and bounded physics queries */
	void GatherCandidateActors(const FVector& Origin, float Range, TArray<AActor*>& OutCandidates) const;

	/** Flushes cached scene/mesh socket components so they are re-resolved on next query */
	void InvalidateSocketCaches();

	/** Advances the antenna sweep position and performs detection for the current beam position */
	void PerformScanSweep(float DeltaTime);

	/** Performs the radar detection model against a candidate actor at the current beam position, populating OutRejectReason if rejected */
	bool EvaluateCandidate(AActor* Candidate, FRadarTrack& OutTrack, FString* OutRejectReason = nullptr) const;

	/** Checks if candidate satisfies DetectableActorTags (checking both actor and component tags) */
	bool CheckCandidateTags(const AActor* Candidate, FString* OutFoundTags = nullptr) const;

	/** Classifies a tracked actor's IFF using IGenericTeamAgentInterface on the radar's owner */
	ERadarIFFResult ClassifyIFF(const AActor* TargetActor) const;

	/** Renders debug visualization for the scan volume and sweeping antenna beam */
	void DrawDebugScanVolume() const;

	// Track Management
	/** Updates all existing track files with new data and prunes stale tracks */
	void UpdateTrackFiles(float DeltaTime);

	/** Creates a new track file from a raw detection */
	int32 CreateTrack(AActor* DetectedActor, const FRadarTrack& RawDetection);

	/** Updates an existing track with a new detection */
	void UpdateTrack(int32 TrackIndex, const FRadarTrack& NewDetection);

	/** Removes stale tracks that have exceeded the timeout */
	void PruneStaleTracks(float DeltaTime);

	/** Performs STT dedicated tracking on the locked target */
	void PerformSTTTracking();

	/** Performs ACM auto-acquisition scan */
	void PerformACMAcquisition();

	/** Performs Spotlight SAR ground-stared tracking, squint Doppler integration, and GMTI detection */
	void PerformSpotlightTracking(float DeltaTime);

	/** Resolves the initial ground intersect point via line trace forward/downward along radar waterline */
	bool ResolveAutoGroundIntersect(FVector& OutGroundLocation) const;

	/** Calculates bearing and elevation from radar to a world position */
	void ComputeBearingElevation(const FVector& TargetPosition, float& OutBearing, float& OutElevation) const;

	/** Smooths velocity estimate for a track (simple exponential filter) */
	FVector SmoothVelocity(const FVector& OldVelocity, const FVector& NewVelocity, float Alpha) const;

	/** Renders debug visualization for all tracks */
	void DrawDebugTracks() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** All active radar track files */
	UPROPERTY(Transient)
	TArray<FRadarTrack> Tracks;
	UPROPERTY(Transient)
	TArray<FRadarTrack> LinkedTracks;
	TMap<int32, TWeakObjectPtr<UAircraftRadarComponent>> LinkedTrackSources;
	int32 DataLinkParticipantID = 0;
	int32 NextLinkedTrackID = -2;
	int32 SelectedLinkedTrackID = -1;
	int32 SelectedContactID = 0;
	int32 NextContactID = 1;
	TMap<TWeakObjectPtr<AActor>, int32> ActorContactIDs;
	int32 AssignContactID(AActor* Actor);
	bool GetBestTrackForContact(int32 ContactID, FRadarTrack& OutTrack) const;
	void RefreshCorrelatedSelection();
	float LastDataLinkReceptionTime = -1000000.0f;
	bool bDataLinkTransmitInhibited = false;
	bool bDataLinkReceiveInhibited = false;
	bool bClientDataLinkConnected = false;
	void PruneLinkedTracks(float WorldTime);

	/** Current antenna azimuth position in degrees relative to aircraft nose */
	UPROPERTY(Transient)
	float CurrentScanAzimuth = 0.0f;

	/** Current elevation bar being scanned (0-indexed) */
	UPROPERTY(Transient)
	int32 CurrentScanBar = 0;

	/** Scan direction: true = sweeping right, false = sweeping left */
	UPROPERTY(Transient)
	bool bScanningRight = true;
	float ScanBarLevel = 0.0f;
	float ScanSweepLevel = 0.0f;
	uint32 ScanProgressRevision = 0;

	/** Track ID of the current STT locked target (-1 = no lock) */
	UPROPERTY(Transient)
	int32 STTLockedTrackID = -1;

	/** Target actor currently locked in STT mode (replicated to all clients for RWR / threat detection) */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Radar|Runtime")
	TObjectPtr<AActor> STTLockedActor = nullptr;

	/** Track ID of the TWS bugged/priority track (-1 = none) */
	UPROPERTY(Transient)
	int32 BuggedTrackID = -1;

	/** The radar mode that was active before entering STT (for BreakLock restoration) */
	UPROPERTY(Transient)
	ERadarOperatingMode PreSTTMode = ERadarOperatingMode::Search;

	/** Monotonically increasing track ID counter */
	UPROPERTY(Transient)
	int32 NextTrackID = 1;

	/** Time accumulator for track update interval throttling */
	UPROPERTY(Transient)
	float TrackUpdateAccumulator = 0.0f;

	UPROPERTY(Transient)
	float ScanSampleAccumulator = 0.0f;

	UPROPERTY(Transient)
	float PreviousSampleAzimuth = 0.0f;

	UPROPERTY(Transient)
	FVector PreviousSocketLocalForward = FVector::ZeroVector;

	UPROPERTY(Transient)
	float SocketSweepDegrees = 0.0f; // Signed rotation; oscillation does not count as a full revolution.

	UPROPERTY(Transient)
	int32 ElectronicBeamIndex = 0;

	/** Local azimuth/elevation centers covered by the latest scan sample. */
	TArray<FVector2D> ActiveSampleBeams;
	float SampleSweepAzHalf = 0.0f;
	float SampleSweepElHalf = 0.0f;

	mutable TArray<TWeakObjectPtr<AActor>> CachedDiscoveryActors;
	mutable float LastCandidateDiscoveryTime = -1000.0f;
	mutable FVector LastCandidateDiscoveryOrigin = FVector::ZeroVector;
	mutable float LastCandidateDiscoveryRange = 0.0f;
	float LastCandidateDebugTime = -1000.0f;
	float LastRadarDebugTime = -1000.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ARadarOperatorLink>> OperatorLinks;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> ActiveRadarOperator;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> AutoAuthorizedController;

	UPROPERTY(Transient)
	TWeakObjectPtr<ARadarOperatorLink> LocalOperatorLink;

	UPROPERTY(Transient)
	float OperatorSnapshotAccumulator = 0.0f;

	UPROPERTY(Transient)
	int32 OperatorSnapshotRevision = 0;
	UPROPERTY(Transient)
	int32 DisplayViewRevision = 1;
	int32 OperatorEventSequence = 0;
	int32 LastAppliedOperatorEventSequence = 0;

	UPROPERTY(Transient)
	int32 ScanSweepCounter = 0;

	UPROPERTY(Transient)
	int32 SARImageRevision = 0;

	bool bHasAppliedOperatorSnapshot = false;
	ERadarOperatingMode LastAppliedOperatorMode = ERadarOperatingMode::Off;

	void RefreshAutomaticOperator();
	void PublishOperatorSnapshot();
	void PruneLaunchedRadarMissiles();
	void BuildLaunchedRadarMissileStatuses(TArray<FRadarLaunchedMissileStatus>& OutMissiles) const;
	void RecordOperatorEvent(ERadarOperatorEventType Type, int32 TrackID = -1,
		bool bSuccess = false, const FRadarTrack* Track = nullptr,
		const FVector& Location = FVector::ZeroVector);
	bool IsCursorWithinLimits(const FRadarCursorState& Cursor) const;
	bool SetDisplayWindowAuthoritative(ERadarDisplayGeometry Geometry, const FRadarDisplayWindow& Window);
	bool SetCursorFromDisplayPoint(const FVector2D& WidgetPosition, const FVector2D& WidgetSize);
	bool ResolveDisplayPointToCursor(const FVector2D& WidgetPosition,
		const FRadarDisplayProjection& Projection, FRadarCursorState& OutCursor) const;
	int32 SubmitControlRequest(ERadarCommandType Command, int32 IntValue = 0, float ValueA = 0.0f,
		float ValueB = 0.0f, const FVector& WorldValue = FVector::ZeroVector,
		const FRadarCursorState& Cursor = FRadarCursorState(),
		ERadarDisplayGeometry Geometry = ERadarDisplayGeometry::BScope,
		APlayerController* RequestingController = nullptr);

	/** Designated ground coordinate in world space for Spotlight SAR */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Radar|Spotlight SAR")
	FVector SpotlightTargetLocation = FVector::ZeroVector;

	/** True if a ground point is currently active and locked */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Radar|Spotlight SAR")
	bool bHasSpotlightPoint = false;

	/** Current SAR dwell integration progress [0.0, 1.0] */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Radar|Spotlight SAR")
	float SpotlightDwellProgress = 0.0f;

	/** Current squint angle in degrees between aircraft velocity and line of sight to ground point */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Radar|Spotlight SAR")
	float SpotlightSquintAngle = 0.0f;

	/** True if aircraft velocity vector points too close to ground target (zero Doppler gradient) */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Radar|Spotlight SAR")
	bool bSpotlightInBlindCone = false;

	/** True if ground target is outside the radar antenna gimbal limits */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Radar|Spotlight SAR")
	bool bSpotlightGimbalExceeded = false;

	/** Optional tracked actor locked as ground target */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> SpotlightTrackedActor = nullptr;

	/** Internal dwell accumulator in seconds */
	UPROPERTY(Transient)
	float SpotlightDwellAccumulator = 0.0f;

	/** Current helmet look direction in world coordinates */
	UPROPERTY(Transient)
	FVector HelmetLookDirection = FVector::ZeroVector;

private:
	/** Finds the array index for a given TrackID. Returns INDEX_NONE if not found. */
	FORCEINLINE int32 FindTrackIndex(int32 TrackID) const
	{
		for (int32 i = 0; i < Tracks.Num(); ++i)
		{
			if (Tracks[i].TrackID == TrackID)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	/** Finds the array index for a given TrackedActor. Returns INDEX_NONE if not found. */
	FORCEINLINE int32 FindTrackIndexByActor(const AActor* InActor) const
	{
		if (!InActor)
		{
			return INDEX_NONE;
		}

		for (int32 i = 0; i < Tracks.Num(); ++i)
		{
			if (Tracks[i].TrackedActor.Get() == InActor)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	/** Cached scene or mesh component containing the radar socket to avoid per-frame component searches */
	UPROPERTY(Transient)
	mutable TWeakObjectPtr<USceneComponent> CachedRadarSocketComponent = nullptr;

	/** Flag indicating whether the radar antenna socket resolution has been performed */
	mutable bool bRadarSocketResolved = false;

	/** Cached scene or mesh component containing the radar altimeter socket to avoid per-frame component searches */
	UPROPERTY(Transient)
	mutable TWeakObjectPtr<USceneComponent> CachedAltimeterSocketComponent = nullptr;

	/** Flag indicating whether the radar altimeter socket resolution has been performed */
	mutable bool bAltimeterSocketResolved = false;

	/** World-space location of the radar altimeter antenna source */
	FVector GetRadarAltimeterLocation() const;

	/** World-space orientation of the radar altimeter antenna source */
	FRotator GetRadarAltimeterRotation() const;

	/** Performs the radar altimeter line trace and updates cached altitude and validity */
	void UpdateRadarAltimeter(float DeltaTime);

	/** Cached radar altitude in cm from the most recent altimeter measurement */
	UPROPERTY(Transient)
	float CachedRadarAltitude = 0.0f;

	/** True if the radar altimeter currently has a valid ground return within limits */
	UPROPERTY(Transient)
	bool bIsRadarAltitudeValid = false;

	/** Time accumulator for altimeter update throttling */
	UPROPERTY(Transient)
	float AltimeterUpdateAccumulator = 0.0f;

	/** In-flight missiles relying on this radar for SARH illumination or command guidance */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<URadarMissileGuidanceComponent>> ActiveGuidingMissiles;
	struct FLaunchedMissileEntry
	{
		TWeakObjectPtr<URadarMissileGuidanceComponent> Missile;
		int32 ID = 0;
	};
	/** Server-owned weak registry; missiles remain listed after switching guidance source. */
	TArray<FLaunchedMissileEntry> LaunchedRadarMissiles;
	int32 NextLaunchedMissileID = 1;
	UPROPERTY(Transient)
	TArray<FRadarLaunchedMissileStatus> ClientLaunchedRadarMissiles;

	/** Manual CW illumination target (for SAM sites / AI script control) */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> ManualCWTargetActor = nullptr;

	/** Whether manual CW illumination is currently enabled */
	UPROPERTY(Transient)
	bool bManualCWIlluminating = false;
};
