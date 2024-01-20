// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "AircraftRadarComponent.generated.h"

class AActor;
class APawn;

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
	VerticalScan UMETA(DisplayName = "Vertical Scan (10°W x ±60°V)"),
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track")
	int32 TrackID = 0;

	/** Underlying actor being tracked (may be null for unresolved contacts) */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	TWeakObjectPtr<AActor> TrackedActor;

	/** Current status of this track */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarTrackStatus Status = ERadarTrackStatus::Search;

	/** IFF classification */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarIFFResult IFFResult = ERadarIFFResult::Unknown;

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
};

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
class WEAPONSYSTEMS_API UAircraftRadarComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAircraftRadarComponent();

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

	/** Current radar operating mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Radar|Configuration")
	ERadarOperatingMode RadarMode = ERadarOperatingMode::Search;

	/** ACM sub-mode (only used when RadarMode == ACM) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Configuration", meta = (EditCondition = "RadarMode == ERadarOperatingMode::AirCombatManeuver", EditConditionHides))
	ERadarACMSubMode ACMSubMode = ERadarACMSubMode::Boresight;

	/** Total azimuth scan width in degrees (e.g. 120 for fighter, 360 for AEW) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Scan Volume", meta = (ClampMin = "5.0", ClampMax = "360.0", EditCondition = "ScanSizePreset == ERadarScanSize::Custom"))
	float AzimuthScanWidth = 120.0f;

	/** Total elevation scan height in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Scan Volume", meta = (ClampMin = "2.0", ClampMax = "120.0"))
	float ElevationScanHeight = 20.0f;

	/** Number of elevation bar scan lines (1, 2, 4, 6, 8) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Scan Volume", meta = (ClampMin = "1", ClampMax = "8"))
	int32 ElevationBars = 4;

	/** Scan center azimuth offset in degrees (TDC cursor slew) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Scan Volume")
	float ScanCenterAzimuth = 0.0f;

	/** Scan center elevation offset in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Scan Volume")
	float ScanCenterElevation = 0.0f;

	/** Scan size preset for quick azimuth configuration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Scan Volume")
	ERadarScanSize ScanSizePreset = ERadarScanSize::Wide_60;

	/** Maximum instrumented detection range in cm (e.g. 30,000,000 cm = 300 km for AN/APG-77) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Range", meta = (ClampMin = "100000.0"))
	float MaxDetectionRange = 15000000.0f;

	/** Minimum detection range in cm (clutter rejection blind zone) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Range", meta = (ClampMin = "0.0"))
	float MinDetectionRange = 50000.0f;

	/** Currently selected display range scale in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Range")
	float CurrentDisplayRange = 7400000.0f;

	/** Selectable range scale presets in cm (e.g. 20nm, 40nm, 80nm, 160nm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Range")
	TArray<float> RangeScalePresets;

	/** Antenna scan rate in degrees per second (e.g. 70°/s for mechanical antenna, 0 for AESA) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "10.0"))
	float ScanRateDegreesPerSecond = 70.0f;

	/** Track update interval in seconds (how often track files are refreshed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.1"))
	float TrackUpdateInterval = 0.5f;

	/** Minimum target Radar Cross Section (m²) detectable at MaxDetectionRange */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.001"))
	float MinimumDetectableRCS = 1.0f;

	/** Default RCS in m² assumed for targets that don't specify one (typical fighter = 3-5 m²) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.01"))
	float DefaultTargetRCS = 3.0f;

	/** Ground/sea clutter rejection effectiveness (0.0 = no rejection, 1.0 = perfect) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClutterRejectionFactor = 0.8f;

	/** Minimum radial velocity in cm/s for Doppler track maintenance (notch filter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.0"))
	float NotchFilterVelocity = 3000.0f;

	/** If true, LOS ray trace checks are performed for look-down / terrain masking */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bEnableTerrainMasking = true;

	/** If true, queries all dynamic object types (Pawn, WorldDynamic, PhysicsBody, Vehicle) to prevent missing pawns with custom collision presets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bQueryAllDynamicObjects = true;

	/** If true, also inspects component tags if the candidate actor itself does not have matching tags */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bSearchComponentTags = true;

	/** If true, checks all targets in scan volume every tick instead of waiting for physical antenna beam sweep (ideal for AESA radar or rapid debugging) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bInstantFullVolumeScan = false;

	/** Radar antenna beam horizontal width in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "1.0", ClampMax = "45.0"))
	float BeamAzimuthWidth = 6.0f;

	/** Radar antenna beam vertical width in degrees (covers elevation bar spacing to eliminate blind gaps) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "1.0", ClampMax = "45.0"))
	float BeamElevationWidth = 10.0f;

	/** Collision channel for target detection overlaps (used when bQueryAllDynamicObjects is false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	TEnumAsByte<ECollisionChannel> DetectionChannel = ECC_Pawn;

	/** Actor tags required for radar detection (if empty, all pawns are candidates) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	TArray<FName> DetectableActorTags;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management", meta = (ClampMin = "5.0", ClampMax = "45.0"))
	float ACMBoresightConeAngle = 10.0f;

	/** Master switch for debug visualization and HUD telemetry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug")
	bool bEnableDebugTraces = false;

	/** If true, renders the 3D scan volume wireframe frustum and elevation bars */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug")
	bool bDrawScanVolume = true;

	/** If true, renders the 3D antenna beam boresight ray and scanning cone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug")
	bool bDrawAntennaBeam = true;

	/** If true, renders 3D track symbology (diamonds, STT reticles, velocity vectors) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug")
	bool bDrawTrackSymbology = true;

	/** If true, renders on-screen live telemetry diagnostics table */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug")
	bool bEnableDiagnosticHUD = true;

	/** Sets the radar operating mode */
	UFUNCTION(BlueprintCallable, Category = "Radar|Control")
	void SetRadarMode(ERadarOperatingMode NewMode);

	/** Returns the current radar mode */
	UFUNCTION(BlueprintPure, Category = "Radar|Control")
	FORCEINLINE ERadarOperatingMode GetRadarMode() const { return RadarMode; }

	/** Returns true if the radar is actively emitting RF energy (not Off, not Standby) */
	UFUNCTION(BlueprintPure, Category = "Radar|Control")
	bool IsRadarEmitting() const;

	/** Sets the ACM sub-mode and switches to ACM mode if not already */
	UFUNCTION(BlueprintCallable, Category = "Radar|Control")
	void SetACMSubMode(ERadarACMSubMode NewSubMode);

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

	/** Returns the current scan antenna azimuth position in degrees (for HUD B-scope rendering) */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	FORCEINLINE float GetCurrentScanAzimuth() const { return CurrentScanAzimuth; }

	/** Returns the current scan antenna elevation bar index */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	FORCEINLINE int32 GetCurrentScanBar() const { return CurrentScanBar; }

	/** Returns the normalized scan position as (Azimuth%, Elevation%) for HUD display */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	FVector2D GetCurrentScanPosition() const;

	/** Returns true if the given actor is within the current scan volume */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan")
	bool IsTargetInScanVolume(AActor* Target) const;

	/** Calculates the maximum detection range for a target with the given RCS in m² */
	UFUNCTION(BlueprintPure, Category = "Radar|Performance")
	float CalculateDetectionRange(float TargetRCS) const;

	/** Checks if a target is terrain-masked (LOS blocked by ground geometry) */
	bool IsTerrainMasked(const FVector& RadarPosition, const FVector& TargetPosition, const AActor* TargetActor = nullptr, FHitResult* OutHit = nullptr) const;

	/** Commands STT lock on a specific track file (transitions radar to STT mode) */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock")
	bool CommandLock(int32 TrackID);

	/** Commands STT lock on a specific target actor by searching active tracks */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock")
	bool CommandLockActor(AActor* TargetActor);

	/**
	 * Acquires or locks a specific actor immediately.
	 * If the actor is already in tracks, locks or bugs it.
	 * If not yet tracked but within radar parameters, evaluates and creates a track immediately.
	 *
	 * @param TargetActor The actor to acquire
	 * @param bForceSTT If true, transitions to STT mode; if false, bugs in TWS or acquires in Search
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock")
	bool AcquireOrLockActor(AActor* TargetActor, bool bForceSTT = false);

	/** Breaks the current STT lock and returns to the previous scan mode */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock")
	void BreakLock();

	/** Bugs/designates a TWS track for priority tracking and weapon cueing (does not change to STT) */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock")
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

	/** Current TDC (Target Designator Control) cursor azimuth offset in degrees relative to antenna scan center */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|HOTAS")
	float TDCCursorAzimuth = 0.0f;

	/** Current TDC cursor range position in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|HOTAS")
	float TDCCursorRange = 3704000.0f; // Default 20nm

	/** Slew the TDC cursor on the radar MFD using joystick input (-1..+1 on X and Y axes) */
	UFUNCTION(BlueprintCallable, Category = "Radar|HOTAS")
	void SlewTDCCursor(float AzimuthInput, float RangeInput, float DeltaTime, float SlewSpeedDegPerSec = 40.0f, float SlewSpeedFractionPerSec = 0.4f);

	/** Returns normalized (X: 0..1, Y: 0..1) TDC cursor coordinates for MFD screen space rendering */
	UFUNCTION(BlueprintPure, Category = "Radar|HOTAS")
	FVector2D GetTDCCursorScreenPosition() const;

	/** HOTAS TMS Up / TDC Depress: Designates track under cursor (Bugs an unbugged track, or locks a bugged track to STT) */
	UFUNCTION(BlueprintCallable, Category = "Radar|HOTAS")
	bool DesignateTrackUnderCursor(float AzimuthGateDegrees = 6.0f, float RangeGatePercent = 0.15f);

	/** HOTAS TMS Down / Undesignate: Breaks STT lock or clears bugged target */
	UFUNCTION(BlueprintCallable, Category = "Radar|HOTAS")
	void HOTAS_Undesignate();

	/** HOTAS TMS Right / Step: Cycles target designation between active tracks */
	UFUNCTION(BlueprintCallable, Category = "Radar|HOTAS")
	bool CycleTargetDesignation(bool bForward = true);

	/** Converts a track's position to normalized B-scope MFD screen coordinates: X (-1..+1), Y (0..1) */
	UFUNCTION(BlueprintPure, Category = "Radar|Display")
	bool GetTrackBScopePosition(int32 TrackID, FVector2D& OutScreenPos) const;

	/** Computes target aspect angle in degrees (-180..+180, 0 = pure tail-on, 180 = pure head-on) for display heading vectors */
	UFUNCTION(BlueprintPure, Category = "Radar|Display")
	float GetTrackAspectAngle(int32 TrackID) const;

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
	/** Advances the antenna sweep position and performs detection for the current beam position */
	void PerformScanSweep(float DeltaTime);

	/** Performs the radar detection model against a candidate actor at the current beam position, populating OutRejectReason if rejected */
	bool EvaluateCandidate(AActor* Candidate, FRadarTrack& OutTrack, FString* OutRejectReason = nullptr) const;

	/** Checks if candidate satisfies DetectableActorTags (checking both actor and component tags) */
	bool CheckCandidateTags(const AActor* Candidate, FString* OutFoundTags = nullptr) const;

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
	UPROPERTY(Transient, Replicated)
	TArray<FRadarTrack> Tracks;

	/** Current antenna azimuth position in degrees relative to aircraft nose */
	UPROPERTY(Transient, Replicated)
	float CurrentScanAzimuth = 0.0f;

	/** Current elevation bar being scanned (0-indexed) */
	UPROPERTY(Transient)
	int32 CurrentScanBar = 0;

	/** Scan direction: true = sweeping right, false = sweeping left */
	UPROPERTY(Transient)
	bool bScanningRight = true;

	/** Track ID of the current STT locked target (-1 = no lock) */
	UPROPERTY(Transient, Replicated)
	int32 STTLockedTrackID = -1;

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

private:
	/** Finds the array index for a given TrackID. Returns INDEX_NONE if not found. */
	int32 FindTrackIndex(int32 TrackID) const;

	/** Finds the array index for a given TrackedActor. Returns INDEX_NONE if not found. */
	int32 FindTrackIndexByActor(const AActor* InActor) const;
};
