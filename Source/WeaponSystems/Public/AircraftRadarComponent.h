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
	MSA UMETA(DisplayName = "MSA (Mechanically Scanned Array)"),
	PESA UMETA(DisplayName = "PESA Electronic Steering"),
	AESA UMETA(DisplayName = "AESA Electronic Steering")
};

/** Captured detection footprint; diagnostics use exactly the same geometry as the sensor. */
USTRUCT()
struct WEAPONSYSTEMS_API FRadarBeamSample
{
	GENERATED_BODY()

	UPROPERTY() FVector Origin = FVector::ZeroVector;
	UPROPERTY() FRotator PlateRotation = FRotator::ZeroRotator;
	UPROPERTY() FVector2D Angles = FVector2D::ZeroVector;
	UPROPERTY() FVector2D HalfWidths = FVector2D(3.0f, 5.0f);
	UPROPERTY() FVector2D SweepHalfWidths = FVector2D::ZeroVector;

	FVector2D GetTargetAngles(const FVector& Position) const;
	bool Contains(const FVector& Position) const;
	float GetGain(const FVector& Position) const;
};

UENUM(BlueprintType)
enum class ERadarCommandType : uint8
{
	SetMode, SetRange, SetACMMode, LockTrack, BugTrack, BreakLock, ClearBug,
	DesignateCursor, SetCursor, SetCursorInput, SetScanVolume, OffsetScanCenter,
	DesignateSpotlight, ClearSpotlight, HelmetCue, ApplyScanPreset, DesignateLinkedTrack,
	SetDisplayGeometry, SetDisplayHeadingUp
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
 * Radar operating mode determining antenna scanning, beam steering, and track maintenance behavior.
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
 * ACM sub-mode for close-range automatic target acquisition in visual combat.
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
 * Lifecycle status of an individual radar track file.
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
 * IFF identification result.
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
 * Standardized azimuth scan volume presets.
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
 * Individual radar track file representing a detected and/or tracked contact.
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FRadarTrack
{
	GENERATED_BODY()

	/** Unique track file identifier assigned by this radar platform. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	int32 TrackID = 0;

	/** Correlated target contact identity; local and remote donor tracks sharing the same actor match this ContactID. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	int32 ContactID = 0;

	/** Underlying actor being tracked (may be null for unresolved or remote contacts). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	TWeakObjectPtr<AActor> TrackedActor;

	/** Current tracking and lock status of this track file. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarTrackStatus Status = ERadarTrackStatus::Search;

	/** Identification Friend or Foe (IFF) classification status. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarIFFResult IFFResult = ERadarIFFResult::Unknown;

	/** Origin source of this track (Local radar measurement vs external Data Link network participant). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarTrackSource Source = ERadarTrackSource::Local;

	/** Data Link participant network ID of the donor radar node (0 if local). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	int32 SourceParticipantID = 0;

	/** Track file identifier on the original donor radar (0 if local). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	int32 SourceTrackID = 0;

	/** True if this track has a confirmed server actor association. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bHasActorAssociation = false;

	/** Last measured world position of the target. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	FVector LastKnownPosition = FVector::ZeroVector;

	/** Kalman-smoothed estimated velocity vector in cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	FVector EstimatedVelocity = FVector::ZeroVector;

	/** Radial closure velocity toward the radar in cm/s (positive = closing, negative = opening). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float ClosureRate = 0.0f;

	/** Azimuth bearing relative to radar plate forward in degrees (-180° to +180°). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float Bearing = 0.0f;

	/** Elevation angle relative to radar plate axes in degrees (-90° to +90°). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float Elevation = 0.0f;

	/** Slant range to contact in Unreal centimeters (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float Range = 0.0f;

	/** Normalized signal return strength / SNR quality (0.0 = sensitivity floor, 1.0 = saturation). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float SignalStrength = 0.0f;

	/** Time in seconds elapsed since the last fresh radar measurement was integrated. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float TrackAge = 0.0f;

	/** Target altitude Above Sea Level (ASL) in centimeters. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float AltitudeASL = 0.0f;

	/** Estimated target compass heading in degrees (0° to 360°). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float TargetHeading = 0.0f;

	/** True if target is currently flying inside the Doppler notch filter (perpendicular crossing). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsNotching = false;

	/** True if active electronic countermeasures / ECM jamming are detected from this contact. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsJamming = false;

	/** True if this track is the dedicated STT beam target or bugged priority contact. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsBeamTarget = false;

	/** True if this track is currently designated as the primary bugged track in TWS mode. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsBugged = false;

	/** Target Radar Cross Section (RCS) in square meters (m²). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float RCS = 0.0f;

	/** Aspect-modified effective Radar Cross Section in square meters (m²). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	float EffectiveRCS = 0.0f;

	/** True if target provided an explicit 'RCS=X' tag; false if using default fallback. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bHasRCSTag = false;

	/** Operational vehicle domain classification (Air, Ground, Sea, Missile). */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	ERadarTargetDomain TargetDomain = ERadarTargetDomain::Air;

	/** True if this track was detected as a surface/ground/naval contact. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsGroundTarget = false;

	/** True if surface contact is a moving ground vehicle exceeding the GMTI velocity threshold. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Track")
	bool bIsGMTIMoving = false;
};

UENUM(BlueprintType)
enum class ERadarMissileGuidanceSource : uint8
{
	Inertial, DataLink, Illumination, OnboardSeeker, Unguided
};

/**
 * Status snapshot of an in-flight radar guided missile associated with this radar.
 */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FRadarLaunchedMissileStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 MissileID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<URadarMissileGuidanceComponent> MissileComponent = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<AActor> MissileActor = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<UAircraftRadarComponent> LaunchRadar = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<UAircraftRadarComponent> RelevantRadar = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<UAircraftRadarComponent> CurrentExternalRadar = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") TObjectPtr<UAircraftRadarComponent> LastExternalRadar = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 LaunchTrackID = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 ContactID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 SourceParticipantID = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") int32 SourceTrackID = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") bool bMadDog = false;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") ERadarMissileFlightPhase FlightPhase = ERadarMissileFlightPhase::PreLaunch;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") ERadarMissileGuidanceSource GuidanceSource = ERadarMissileGuidanceSource::Inertial;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") bool bOnboardSeekerActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") float FlightTimeSeconds = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Radar|Launched Missiles") float TimeToActiveSeconds = -1.0f;
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
 * Universal airborne combat and surveillance radar simulation.
 * Simulates pulse-Doppler search (RWS), track-while-scan (TWS), single-target track (STT),
 * air combat maneuvering (ACM), surface mapping (GM/SS), and spotlight SAR dwell imaging.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UAircraftRadarComponent : public UActorComponent, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FRadarDisplaySelectionTest;
	friend class FAircraftDataLinkRoutingTest;
	friend class FRadarScanDriveModesTest;
#endif

public:
	UAircraftRadarComponent();

	// ========================================================================
	// 1. Radar Configuration
	// ========================================================================

	/** Current radar operating mode (Off, Standby, Search, TWS, STT, ACM, GM, SS, SAR). Replicated to clients. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_RadarMode, Category = "Radar|Configuration")
	ERadarOperatingMode RadarMode = ERadarOperatingMode::Search;

	/** Common origin and plate axes for every drive/mode; empty or missing sockets use the owner root transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Configuration", meta = (DisplayName = "Radar Socket"))
	FName RadarSocketName = NAME_None;

	/** Stable cockpit display/cursor axes only; never drives detection. None uses the owner root. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Display & Cursor")
	FName RadarDisplayReferenceComponentName = NAME_None;

	/** Local translation offset applied to antenna origin for elevating turrets or adjusting mast sensors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Configuration")
	FVector RadarLocationOffset = FVector::ZeroVector;

	/** If true, treats the platform as an omnidirectional or dome radar, bypassing antenna azimuth gimbal limits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Configuration")
	bool bOmnidirectionalTracking = false;

	// ========================================================================
	// 2. Scan Volume & Antenna Mechanics
	// ========================================================================

	/** Antenna drive mechanism; all drives scan relative to the radar socket transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume")
	ERadarScanDrive ScanDrive = ERadarScanDrive::MSA;

	/** MSA: add virtual steering relative to the socket. Disable when AnimBP steers the plate; commands still advance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume", meta = (EditCondition = "ScanDrive == ERadarScanDrive::MSA", EditConditionHides))
	bool bVirtuallySweepBeam = true;

	/** Standardized azimuth scan volume preset. Locked to custom when manually setting width. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume")
	ERadarScanSize ScanSizePreset = ERadarScanSize::Full_120;

	/** Total horizontal azimuth scan width in degrees (e.g. 120° for nose radar, 360° for AEW). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Scan Volume", meta = (ClampMin = "5.0", ClampMax = "360.0", EditCondition = "ScanSizePreset == ERadarScanSize::Custom"))
	float AzimuthScanWidth = 120.0f;

	/** Total vertical elevation scan height in degrees across all elevation bars. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume", meta = (ClampMin = "2.0", ClampMax = "120.0"))
	float ElevationScanHeight = 20.0f;

	/** Number of horizontal elevation scan bars (1, 2, 4, 6, 8). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume", meta = (ClampMin = "1", ClampMax = "8"))
	int32 ElevationBars = 4;

	/** Scan center azimuth offset in degrees from socket boresight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume")
	float ScanCenterAzimuth = 0.0f;

	/** Scan center elevation offset in degrees from socket boresight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume")
	float ScanCenterElevation = 0.0f;

	/** Maximum physical antenna gimbal limit in azimuth (degrees from antenna boresight, e.g. ±60°). Governs STT bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume", meta = (ClampMin = "10.0", ClampMax = "180.0"))
	float MaxAntennaGimbalAzimuth = 60.0f;

	/** Maximum physical antenna gimbal limit in elevation (degrees from antenna boresight, e.g. ±60°). Governs STT bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan Volume", meta = (ClampMin = "10.0", ClampMax = "90.0"))
	float MaxAntennaGimbalElevation = 60.0f;

	// ========================================================================
	// 3. Performance & Ticking
	// ========================================================================

	/** Antenna sweep rate in degrees per second for mechanical scan drives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.1", UIMin = "1.0", UIMax = "360.0", EditCondition = "ScanDrive == ERadarScanDrive::MSA"))
	float ScanRateDegreesPerSecond = 70.0f;

	/** Spatial sampling interval in seconds between beam detection sweeps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Performance", meta = (ClampMin = "0.016", ClampMax = "1.0"))
	float ScanSampleInterval = 0.1f;

	/** Interval in seconds between track file aging, Kalman velocity smoothing, and timeout pruning passes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.05", UIMin = "0.1", UIMax = "2.0"))
	float TrackUpdateInterval = 0.5f;

	/** Number of concurrent beams steered per sample in AESA mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Performance", meta = (ClampMin = "1", ClampMax = "32", EditCondition = "ScanDrive == ERadarScanDrive::AESA"))
	int32 AESABeamsPerSample = 4;

	/** If true, conducts periodic spatial overlap queries to discover actors not pre-registered with the combat subsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bEnablePhysicsCandidateDiscovery = true;

	/** Interval in seconds between spatial overlap candidate discovery passes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Performance", meta = (ClampMin = "0.05", ClampMax = "5.0", EditCondition = "bEnablePhysicsCandidateDiscovery"))
	float CandidateDiscoveryInterval = 0.5f;

	/** Platform displacement threshold in cm triggering immediate candidate re-discovery. */
	UPROPERTY(EditDefaultsOnly, Category = "Radar|Performance", meta = (ClampMin = "1000.0", EditCondition = "bEnablePhysicsCandidateDiscovery"))
	float DiscoveryDisplacementThresholdCm = 50000.0f;

	/** Range delta threshold in cm triggering candidate re-discovery. */
	UPROPERTY(EditDefaultsOnly, Category = "Radar|Performance", meta = (ClampMin = "10.0", EditCondition = "bEnablePhysicsCandidateDiscovery"))
	float DiscoveryRangeDeltaThresholdCm = 100.0f;

	/** Additional radial buffer in cm added to detection range for candidate discovery sweeps. */
	UPROPERTY(EditDefaultsOnly, Category = "Radar|Performance", meta = (ClampMin = "1000.0", EditCondition = "bEnablePhysicsCandidateDiscovery"))
	float CandidateDiscoveryExpandedRadiusCm = 100000.0f;

	/** Maximum candidate actors evaluated per beam sample to avoid frame spikes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Performance", meta = (ClampMin = "16", ClampMax = "4096"))
	int32 MaxCandidatesPerSample = 512;

	/** If true, candidate discovery queries all dynamic object types (Pawn, WorldDynamic, PhysicsBody, Vehicle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (EditCondition = "bEnablePhysicsCandidateDiscovery"))
	bool bQueryAllDynamicObjects = true;

	/** Collision channel used for candidate overlap discovery when bQueryAllDynamicObjects is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (EditCondition = "bEnablePhysicsCandidateDiscovery && !bQueryAllDynamicObjects"))
	TEnumAsByte<ECollisionChannel> DetectionChannel = ECC_Pawn;

	/** Receiver dynamic range in dB above thermal sensitivity threshold for SignalStrength normalization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "10.0", ClampMax = "80.0", UIMin = "20.0", UIMax = "60.0"))
	float ReceiverDynamicRangeDB = 40.0f;

	/** Minimum radial closure speed in cm/s below which ground clutter notch filtering rejects targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10000.0"))
	float NotchFilterVelocity = 3000.0f;

	/** If true, STT mode retains angle-tracking lock when target enters the Doppler notch even if range degrades. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Performance")
	bool bSTTAngleTrackThroughNotch = true;

	// ========================================================================
	// 4. Detection & Radar Equation (RCS)
	// ========================================================================

	/** Maximum instrumented radar detection range in cm (e.g. 15,000,000 cm = 150 km). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS", meta = (ClampMin = "100000.0"))
	float MaxDetectionRange = 15000000.0f;

	/** Minimum detection range in cm (clutter blind zone around antenna). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS", meta = (ClampMin = "0.0"))
	float MinDetectionRange = 50000.0f;

	/** Currently selected display range scale in cm. Replicated to clients. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentDisplayRange, Category = "Radar|Detection & RCS", meta = (ClampMin = "1000.0"))
	float CurrentDisplayRange = 7400000.0f;

	/** Selectable range scale presets in cm for MFD range stepping (e.g. 20nm, 40nm, 80nm, 160nm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS")
	TArray<float> RangeScalePresets;

	/** Minimum target Radar Cross Section in m² detectable at MaxDetectionRange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS", meta = (ClampMin = "0.001"))
	float MinimumDetectableRCS = 1.0f;

	/** Baseline RCS in m² assigned to target actors lacking an explicit 'RCS=X' tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS", meta = (ClampMin = "0.0001"))
	float DefaultTargetRCS = 99.99f;

	/** If true, parses 'RCS=X' tags from candidate actors and components. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS")
	bool bEnableTargetRCSTagParsing = true;

	/** If true, modifies effective target RCS based on relative aspect angle (beam broadside vs nose-on). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS")
	bool bEnableAspectAngleRCS = true;

	/** Multiplier applied to target RCS when presenting a beam/broadside aspect to the radar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS", meta = (ClampMin = "1.0", ClampMax = "20.0", EditCondition = "bEnableAspectAngleRCS"))
	float RCSAspectBeamMultiplier = 3.5f;

	/** Multiplier applied to target RCS when presenting a tail aspect (engine cavity reflections). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS", meta = (ClampMin = "1.0", ClampMax = "10.0", EditCondition = "bEnableAspectAngleRCS"))
	float RCSAspectTailMultiplier = 1.8f;

	/** If true, evaluates terrain line-of-sight ray traces against static geometry for look-down masking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS")
	bool bEnableTerrainMasking = true;

	/** If true, candidate tag checks inspect child component tags if root actor tags do not match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS")
	bool bSearchComponentTags = true;

	/** Horizontal radar antenna beam half-power width in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS", meta = (ClampMin = "1.0", ClampMax = "45.0"))
	float BeamAzimuthWidth = 6.0f;

	/** Vertical radar antenna beam half-power width in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS", meta = (ClampMin = "1.0", ClampMax = "45.0"))
	float BeamElevationWidth = 10.0f;

	/** Actor tags required for radar detection. If empty, all valid pawns/vehicles within envelope are candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Detection & RCS")
	TArray<FName> DetectableActorTags;

	// ========================================================================
	// 5. Track Management
	// ========================================================================

	/** Maximum total track files maintained simultaneously by the radar computer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Track Management", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxTrackFiles = 64;

	/** Maximum simultaneous TWS track files tracked with priority velocity extrapolation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management", meta = (ClampMin = "1", ClampMax = "50"))
	int32 MaxSimultaneousTWSTracks = 10;

	/** Baseline duration in seconds without fresh radar returns before a track file is dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Track Management", meta = (ClampMin = "1.0"))
	float TrackDropTimeout = 8.0f;

	/** Multiplier applied to full antenna revisit frame time to ensure tracks do not drop mid-sweep. */
	UPROPERTY(EditDefaultsOnly, Category = "Radar|Track Management", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float TrackDropMarginMultiplier = 1.5f;

	/** Maximum duration in seconds for dead-reckoning extrapolation when contact is temporarily obstructed. */
	UPROPERTY(EditDefaultsOnly, Category = "Radar|Track Management", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MaxDeadReckoningSeconds = 2.0f;

	/** Exponential smoothing weight for track velocity estimation (0.0 = frozen, 1.0 = raw measurement). */
	UPROPERTY(EditDefaultsOnly, Category = "Radar|Track Management", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float VelocitySmoothingAlpha = 0.35f;

	// ========================================================================
	// 6. Air Combat Maneuver (ACM) Dogfight Modes
	// ========================================================================

	/** Active ACM acquisition sub-mode (Boresight cone, Vertical Scan swath, HMD Helmet Cue, Slew Acquisition). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Air Combat Maneuver")
	ERadarACMSubMode ACMSubMode = ERadarACMSubMode::Boresight;

	/** Maximum slant range in cm within which the radar automatically locks contacts in ACM modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Air Combat Maneuver", meta = (ClampMin = "100000.0"))
	float ACMAutoLockRange = 1800000.0f;

	/** Half-angle in degrees of the ACM Boresight acquisition cone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Air Combat Maneuver", meta = (ClampMin = "2.0", ClampMax = "45.0"))
	float ACMBoresightConeAngle = 10.0f;

	/** Total azimuth width in degrees of the ACM Vertical Scan swath (centered on canopy centerline). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Air Combat Maneuver", meta = (ClampMin = "2.0", ClampMax = "30.0"))
	float ACMVerticalScanAzimuthWidth = 10.0f;

	/** Lower elevation boundary in degrees for ACM Vertical Scan (below waterline). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Air Combat Maneuver", meta = (ClampMin = "-30.0", ClampMax = "10.0"))
	float ACMVerticalScanMinElevation = -10.0f;

	/** Upper elevation boundary in degrees for ACM Vertical Scan (high canopy lift vector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Air Combat Maneuver", meta = (ClampMin = "15.0", ClampMax = "85.0"))
	float ACMVerticalScanMaxElevation = 60.0f;

	// ========================================================================
	// 7. Synthetic Aperture Radar (SAR) & Spotlight
	// ========================================================================

	/** Radius in cm of the high-resolution SAR Spotlight ground patch (e.g. 200,000 cm = 2 km). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "10000.0", ClampMax = "1000000.0"))
	float SpotlightPatchRadius = 200000.0f;

	/** Dwell integration duration in seconds required to synthesize full cross-range SAR image resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float SpotlightDwellDuration = 2.5f;

	/** Minimum squint angle in degrees relative to flight path below which Doppler gradient collapses into blind cone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "2.0", ClampMax = "30.0"))
	float SpotlightMinSquintAngle = 10.0f;

	/** Maximum squint angle in degrees for Spotlight SAR image synthesis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "30.0", ClampMax = "85.0"))
	float SpotlightMaxSquintAngle = 75.0f;

	/** If true, automatically traces forward along antenna waterline to establish terrain patch if none designated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR")
	bool bSpotlightAutoGroundIntersect = true;

	/** Default slant range in cm for ground patch projection if line trace misses terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "100000.0", EditCondition = "bSpotlightAutoGroundIntersect"))
	float SpotlightDefaultSlantRange = 2500000.0f;

	/** Downward pitch angle in degrees from radar centerline for auto-intersect line trace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "-45.0", ClampMax = "0.0", EditCondition = "bSpotlightAutoGroundIntersect"))
	float SpotlightDefaultPitchAngle = -12.0f;

	/** Ground velocity threshold in cm/s to classify a surface contact as moving (GMTI) vs stationary clutter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Spotlight SAR", meta = (ClampMin = "10.0"))
	float GMTIVelocityThreshold = 150.0f;

	// ========================================================================
	// 8. Target Domain Classification
	// ========================================================================

	/** Allowed target vehicle domains in Air-to-Air modes (Search, TWS, ACM, STT). Defaults to [Air]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Target Classification")
	TArray<ERadarTargetDomain> AirModeAllowedDomains = { ERadarTargetDomain::Air };

	/** Allowed target vehicle domains in Ground Mapping (GM) mode. Defaults to [Ground]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Target Classification")
	TArray<ERadarTargetDomain> GroundMappingAllowedDomains = { ERadarTargetDomain::Ground };

	/** Allowed target vehicle domains in Sea Search (SS) mode. Defaults to [Sea]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Target Classification")
	TArray<ERadarTargetDomain> SeaSearchAllowedDomains = { ERadarTargetDomain::Sea };

	/** Allowed target vehicle domains in Spotlight SAR mode. Defaults to [Ground, Sea]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Target Classification")
	TArray<ERadarTargetDomain> SpotlightAllowedDomains = { ERadarTargetDomain::Ground, ERadarTargetDomain::Sea };

	// ========================================================================
	// 9. Identification Friend or Foe (IFF)
	// ========================================================================

	/** If true, interrogates contacts using IGenericTeamAgentInterface and transponders to classify IFF. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|IFF")
	bool bEnableIFF = true;

	/** Attitude assigned when an interrogated contact has no transponder or team interface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|IFF", meta = (EditCondition = "bEnableIFF"))
	EIFFUnknownAttitude UnknownContactAttitude = EIFFUnknownAttitude::Neutral;

	/** Replicated faction/team identifier for this radar platform (0-254 = Factions, 255 = Neutral). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_TeamID, Category = "Radar|IFF")
	uint8 TeamID = 1;

	/** Replicated Mode 3/A transponder squawk code. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_SquawkCode, Category = "Radar|IFF", meta = (ClampMin = "0", ClampMax = "7777"))
	int32 SquawkCode = 1200;

	// ========================================================================
	// 10. Radar Altimeter (RALT / AGL) Subsystem
	// ========================================================================

	/** If true, operates a high-fidelity radar altimeter measuring height Above Ground Level (AGL) independently of nose EMCON. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter")
	bool bEnableRadarAltimeter = true;

	/** Socket name on aircraft mesh representing the downward-looking radar altimeter antenna. None falls back to root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter"))
	FName RadarAltimeterSocketName = NAME_None;

	/** Maximum operational altitude in cm above which the radar altimeter indicates out-of-limits (e.g. 152,400 cm = 5,000 ft). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", ClampMin = "1000.0"))
	float MaxRadarAltitude = 152400.0f;

	/** Maximum pitch or bank attitude angle in degrees before altimeter beam breaks ground reflection lock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", ClampMin = "10.0", ClampMax = "89.0"))
	float MaxAltimeterAttitudeAngle = 50.0f;

	/** If true, executes multi-ray conical sampling to determine true first-return obstacle clearance; false uses single vertical nadir. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter"))
	bool bAltimeterConicalSampling = true;

	/** Full conical beamwidth in degrees for first-return terrain proximity evaluation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter && bAltimeterConicalSampling", ClampMin = "10.0", ClampMax = "90.0"))
	float AltimeterBeamwidthDegrees = 45.0f;

	/** Smoothing speed for altimeter height filter (higher = more responsive, lower = smoother). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", ClampMin = "1.0", ClampMax = "50.0"))
	float AltimeterSmoothingSpeed = 15.0f;

	/** Interval in seconds between altimeter ground trace updates (default ~30 Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Altimeter", meta = (EditCondition = "bEnableRadarAltimeter", ClampMin = "0.01", UIMin = "0.01", UIMax = "0.2"))
	float AltimeterUpdateInterval = 0.033f;

	// ========================================================================
	// 11. Tactical Data Link Network
	// ========================================================================

	/** Enables participation in tactical data link networks for track sharing and cooperative engagement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link")
	bool bEnableDataLink = false;

	/** Radio network identifier. Only nodes with matching network IDs can communicate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink"))
	FName DataLinkNetworkID = NAME_None;

	/** Platform classification broadcast to network participants (Fighter, AWACS, Surface, Ground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink"))
	EDataLinkPlatformType DataLinkPlatformType = EDataLinkPlatformType::Fighter;

	/** If true, transmits local radar tracks across the tactical data link network. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink"))
	bool bContributeDataLinkTracks = true;

	/** If true, receives and displays external tracks donated by network participants. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink"))
	bool bReceiveDataLinkTracks = true;

	/** If true, acts as a tactical message relay, forwarding packets to nodes out of direct transmitter range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink"))
	bool bRelayDataLinkReports = false;

	/** Allows guided radar missiles to be launched against remote tracks donated via data link without local radar lock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink"))
	bool bAllowRemoteWeaponSupport = false;

	/** Restricts data link communication strictly to verified friendly units (rejects neutral or unknown transponders). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink"))
	bool bRestrictDataLinkToFriendly = false;

	/** Maximum line-of-sight radio transmission and reception range in centimeters (e.g. 20,000,000 cm = 200 km). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink", ClampMin = "1000.0"))
	float DataLinkRangeCm = 20000000.0f;

	/** Duration in seconds without receiving network heartbeats before declaring data link connection lost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink", ClampMin = "0.1"))
	float DataLinkDesyncSeconds = 3.0f;

	/** Duration in seconds before an unrefreshed remote data link track expires and is pruned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink", ClampMin = "0.1"))
	float DataLinkTrackExpirySeconds = 5.0f;

	/** Maximum number of external data link tracks stored in memory simultaneously. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink", ClampMin = "1", ClampMax = "512"))
	int32 MaxLinkedDataLinkTracks = 128;

	/** Maximum number of contact ID mappings retained in local table before garbage collection. */
	UPROPERTY(EditDefaultsOnly, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink", ClampMin = "64", ClampMax = "1024"))
	int32 MaxTrackedContactIDs = 256;

	/** If true, correlates local radar returns and remote data link tracks of the same target into a single ContactID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink"))
	bool bEnableTrackCorrelation = true;

	/** Maximum age in seconds for a local track return to maintain primary authority over a shared ContactID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Data Link", meta = (EditCondition = "bEnableDataLink && bEnableTrackCorrelation", ClampMin = "0.1"))
	float LocalCorrelationFreshnessSeconds = 1.5f;

	// ========================================================================
	// 12. Cockpit Display Projection & Target Cursor (TDC)
	// ========================================================================

	/** Master switch enabling target designator cursor (TDC) input, slew, and display symbology. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Display & Cursor")
	bool bEnableTargetCursor = true;

	/** Rescales the cursor's physical range when the display range changes to preserve its
	 *  position on both B-scope and PPI. Azimuth and elevation stay fixed; cursor limits still apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Display & Cursor", meta = (EditCondition = "bEnableTargetCursor"))
	bool bPreserveCursorDisplayPositionOnRangeChange = false;

	/** B-scope horizontal cursor slew speed in degrees per second at standard display zoom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Display & Cursor", meta = (ClampMin = "1.0", UIMin = "5.0", UIMax = "120.0", EditCondition = "bEnableTargetCursor"))
	float CursorAzimuthSpeed = 40.0f;

	/** B-scope vertical cursor slew speed as a fraction of full range scale per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Display & Cursor", meta = (ClampMin = "0.05", ClampMax = "2.0", UIMin = "0.1", UIMax = "1.0", EditCondition = "bEnableTargetCursor"))
	float CursorRangeSpeedFraction = 0.4f;

	/** PPI top-down cursor slew speed as a fraction of visible display canvas dimensions per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Display & Cursor", meta = (ClampMin = "0.05", ClampMax = "2.0", EditCondition = "bEnableTargetCursor"))
	float CursorDisplaySpeedFraction = 0.4f;

	/** Target acquisition gate radius as a fraction of visible display dimensions for cursor designation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Display & Cursor", meta = (ClampMin = "0.005", ClampMax = "0.08", EditCondition = "bEnableTargetCursor"))
	float CursorSelectionRadiusFraction = 0.04f;

	/** Shared operator display view configuration (Geometry and Heading-Up orientation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Display & Cursor")
	FRadarDisplayView DisplayView;

	// ========================================================================
	// 13. Multiplayer & Multi-Crew Networking
	// ========================================================================

	/** Cadence in seconds at which authoritative radar snapshots and launched missile tracks are replicated to crew links. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Multiplayer", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float OperatorSnapshotIntervalSeconds = 0.2f;

	// ========================================================================
	// 14. Debug & Diagnostics Visualization
	// ========================================================================

	/** Master switch enabling 3D debug visualizations in the world and telemetry logging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug")
	bool bEnableDebugTraces = false;

	/** If true, 3D debug lines and wireframes render only when the owning pawn is locally controlled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces"))
	bool bDebugOnlyPlayerControlled = false;

	/** Renders the 3D antenna scan frustum and elevation bar lines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces"))
	bool bDrawScanVolume = true;

	/** Renders actual sampled beam footprints/directions and terrain-query hit rays, including every AESA visit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces"))
	bool bDrawAntennaBeam = true;

	/** Renders 3D track diamonds, STT reticles, and velocity vectors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces"))
	bool bDrawTrackSymbology = true;

	/** Renders on-screen live diagnostics telemetry HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces"))
	bool bEnableDiagnosticHUD = true;

	/** Renders 3D cursor position in space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces && bEnableTargetCursor"))
	bool bDebugCursor = true;

	/** Logs candidate rejection reasons to log. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces"))
	bool bDebugCandidateRejections = false;

	/** Visualizes candidate discovery overlap batches and candidate counts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces && bEnablePhysicsCandidateDiscovery"))
	bool bDebugPhysicsDiscovery = false;

	/** Visualizes the live plate axes and the independent cockpit display axes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Debug", meta = (EditCondition = "bEnableDebugTraces"))
	bool bDebugReferenceAxes = false;

	// ========================================================================
	// Events & Delegates
	// ========================================================================

	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarContactNewSignature OnRadarContactNew;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarContactUpdatedSignature OnRadarContactUpdated;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarContactLostSignature OnRadarContactLost;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarAllContactsClearedSignature OnRadarAllContactsCleared;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarLockAcquiredSignature OnRadarLockAcquired;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarLockLostByIDSignature OnRadarLockLost;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarModeChangedSignature OnRadarModeChanged;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnScanSweepCompleteSignature OnScanSweepComplete;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarTargetNotchingSignature OnRadarTargetNotching;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarTrackSelectedSignature OnRadarTrackSelected;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarTrackDeselectedSignature OnRadarTrackDeselected;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarScanProgressSignature OnScanProgressUpdated;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarCursorMovedSignature OnRadarCursorMoved;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarCursorDesignatedSignature OnRadarCursorDesignated;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarDisplayRangeChangedSignature OnRadarDisplayRangeChanged;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnSARImageReadySignature OnSARImageReady;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarSnapshotReadySignature OnRadarSnapshotReady;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarDisplayStateUpdatedSignature OnRadarDisplayStateUpdated;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnRadarCommandResultSignature OnRadarCommandResult;
	UPROPERTY(BlueprintAssignable, Category = "Radar|Events") FOnCombatTeamChangedSignature OnTeamChanged;

	// ========================================================================
	// Unified Control & Designation API
	// Automatically dispatches authoritative actions on server or submits
	// operator commands when invoked by client/crew interfaces.
	// ========================================================================

	/**
	 * Sets the radar operating mode (Off, Standby, RWS, TWS, STT, ACM, GM, SS, SAR).
	 * @param NewMode Desired operating mode.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if the mode was changed or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Control", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool SetRadarMode(ERadarOperatingMode NewMode, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Sets the radar display range scale in centimeters.
	 * @param NewRangeCm Desired display range scale in cm.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if the display range scale was changed or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Range", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool SetDisplayRange(float NewRangeCm, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Cycles the display range scale up or down through RangeScalePresets.
	 * @param bIncrease True to increase range scale, false to decrease.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if the display range scale was cycled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Range", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool CycleRangeScale(bool bIncrease = true, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Locks a target track file into Single Target Track (STT) mode.
	 * @param TrackID The unique ID of the track file to lock.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if lock was acquired or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool LockTrack(int32 TrackID, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Locks a specific target actor by resolving its active track file.
	 * @param TargetActor Actor to lock.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if target actor was locked.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool LockActor(AActor* TargetActor, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Acquires or locks a specific actor immediately.
	 * If the actor is already tracked, locks or bugs it.
	 * If not yet tracked, evaluates and creates a track immediately if within envelope.
	 * @param TargetActor Target actor to acquire.
	 * @param bForceSTT If true, acquires directly into STT mode; otherwise bugs in TWS or acquires in Search.
	 * @return True if target was acquired or locked.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Lock")
	bool AcquireOrLockActor(AActor* TargetActor, bool bForceSTT = false);

	/**
	 * Designates/bugs a Track-While-Scan (TWS) track for priority tracking and weapon cueing.
	 * @param TrackID Unique ID of the track file to designate.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if track was bugged or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool BugTrack(int32 TrackID, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Breaks the current STT lock and returns to the previous scan mode.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if lock was broken or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool BreakLock(bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Clears the priority bug designation on a TWS track.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if bug was cleared or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool ClearBugTrack(bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Sets the sub-mode for close-range Air Combat Maneuver (ACM) auto-acquisition.
	 * Automatically switches RadarMode to AirCombatManeuver if not already active.
	 * @param NewSubMode ACM sub-mode (Boresight, VerticalScan, HelmetCue, SlewAcquisition).
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if sub-mode was changed or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Air Combat Maneuver", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool SetACMSubMode(ERadarACMSubMode NewSubMode, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Configures custom antenna scan volume dimensions.
	 * Sets ScanSizePreset to Custom automatically.
	 * @param Azimuth Total azimuth scan width in degrees (5° to 360°).
	 * @param Elevation Total elevation scan height in degrees (2° to 120°).
	 * @param Bars Number of elevation scan bars (1, 2, 4, 6, 8).
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if scan volume was configured or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Scan Volume", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool SetScanVolume(float Azimuth, float Elevation, int32 Bars, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Applies a standardized scan azimuth preset (Narrow 20°, Medium 40°, Wide 60°, Full 120°).
	 * @param Preset Predefined scan size preset.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if preset was applied or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Scan Volume", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool ApplyScanSizePreset(ERadarScanSize Preset, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Offsets the scan volume center azimuth and elevation (TDC antenna slew).
	 * @param AzDelta Azimuth angle offset delta in degrees.
	 * @param ElDelta Elevation angle offset delta in degrees.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if scan center was offset or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Scan Volume", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool OffsetScanCenter(float AzDelta, float ElDelta, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Designates a ground coordinate in world space for Spotlight Synthetic Aperture Radar (SAR) staring.
	 * @param WorldLocation Target terrain location in world space coordinates.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if spotlight coordinate was designated or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Spotlight SAR", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool DesignateSpotlightPoint(const FVector& WorldLocation, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Designates a specific actor on the ground for Spotlight SAR slaved tracking.
	 * @param TargetActor Target actor on surface.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if spotlight actor was designated or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Spotlight SAR", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool DesignateSpotlightActor(AActor* TargetActor, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Clears the active Spotlight SAR ground target coordinate.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if spotlight target was cleared or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Spotlight SAR", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool ClearSpotlightTarget(bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Sets the helmet-mounted display (HMD) or pilot gaze look direction in world space for HelmetCue ACM acquisition.
	 * @param InWorldDirection Normalized world direction vector.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if helmet direction was set or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Air Combat Maneuver", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool SetHelmetLookDirection(const FVector& InWorldDirection, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Moves the radar Target Designator Control (TDC) cursor on the active display using 2D axis inputs.
	 * @param DeltaAxis Movement input vector (X: Azimuth/Horizontal, Y: Range/Vertical).
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if cursor movement was processed or dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Display & Cursor", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool MoveTDCCursor(FVector2D DeltaAxis, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Sets the TDC cursor position directly from a 2D screen/widget coordinate.
	 * @param DisplayPosition Center-relative widget translation matching ProjectWorldToDisplay.
	 * @param WidgetSize Dimensions of the display canvas in Slate pixels.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if cursor position was set or dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Display & Cursor", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool SetTDCCursorFromDisplayPosition(const FVector2D& DisplayPosition, const FVector2D& WidgetSize, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Designates the track or contact currently under the TDC cursor on the active display.
	 * Automatically uses the active display geometry and configured selection radius without requiring manual pins.
	 * Bugs an unbugged track, locks a bugged track into STT, or designates a remote data link track.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if a track under the cursor was successfully designated or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Display & Cursor", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool DesignateUnderCursor(bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Sets the projection geometry mode for cockpit radar displays (B-Scope or PPI).
	 * @param Geometry Display projection geometry.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if display geometry was set or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Display & Cursor", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool SetDisplayGeometry(ERadarDisplayGeometry Geometry, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Toggles heading-up versus north-up orientation for plan displays.
	 * @param bHeadingUp True for heading-up aircraft-referenced view; false for north-up stabilized map.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if heading-up orientation was set or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Display & Cursor", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool SetDisplayHeadingUp(bool bHeadingUp, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	/**
	 * Designates an external track received over Tactical Data Link.
	 * @param TrackID The negative ID identifying the remote data link track.
	 * @param bAuthoritative If true, executes directly on server.
	 * @param Controller Optional requesting player controller for crew validation.
	 * @return True if data link track was designated or request dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radar|Data Link", meta = (AdvancedDisplay = "bAuthoritative,Controller"))
	bool DesignateLinkedTrack(int32 TrackID, bool bAuthoritative = true, APlayerController* Controller = nullptr);

	// ========================================================================
	// Getters, Queries & Hardware Transform Resolvers
	// ========================================================================

	/** Returns the current radar operating mode. */
	UFUNCTION(BlueprintPure, Category = "Radar|Control")
	FORCEINLINE ERadarOperatingMode GetRadarMode() const { return RadarMode; }

	/** Returns true if the radar is actively emitting RF energy (not Off and not Standby). */
	UFUNCTION(BlueprintPure, Category = "Radar|Control")
	bool IsRadarEmitting() const;

	/** Returns the current ACM sub-mode. */
	UFUNCTION(BlueprintPure, Category = "Radar|Air Combat Maneuver")
	FORCEINLINE ERadarACMSubMode GetACMSubMode() const { return ACMSubMode; }

	/** Sets the socket name used as the radar antenna origin and boresight axis. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Configuration")
	void SetRadarSocketName(FName InSocketName);

	/** Returns the socket name used as the radar antenna origin. */
	UFUNCTION(BlueprintPure, Category = "Radar|Configuration")
	FORCEINLINE FName GetRadarSocketName() const { return RadarSocketName; }

	/** Returns the world-space location of the radar antenna source. */
	UFUNCTION(BlueprintPure, Category = "Radar|Source")
	FVector GetRadarLocation() const;

	/** Returns the world-space rotation/orientation of the radar antenna source. */
	UFUNCTION(BlueprintPure, Category = "Radar|Source")
	FRotator GetRadarRotation() const;

	/** Returns both world-space location and rotation of the radar antenna source. */
	UFUNCTION(BlueprintPure, Category = "Radar|Source")
	void GetRadarSourceTransform(FVector& OutLocation, FRotator& OutRotation) const;

	/** Returns the stable cockpit frame used only for display/cursor projection. */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	void GetRadarDisplayReferenceTransform(FVector& OutLocation, FRotator& OutRotation) const;

	/** Creates a display projection structure configured for MFD screen coordinate conversions. */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	FRadarDisplayProjection MakeDisplayProjection(const FVector2D& WidgetTopLeft, const FVector2D& WidgetSize,
		ERadarDisplayGeometry Geometry = ERadarDisplayGeometry::BScope, bool bHeadingUp = true) const;

	/** Projects a world location into center-anchored MFD canvas coordinates (0, 0 is display center). */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	bool ProjectWorldToDisplay(const FVector& WorldLocation, FVector2D WidgetSize, FVector2D& OutWidgetPosition) const;

	/** Returns the current TDC cursor state struct (azimuth, elevation, range, world point). */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	FRadarCursorState GetTDCCursorState() const;

	/** Returns true if the target designator cursor subsystem is enabled. */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	bool IsTargetCursorEnabled() const { return bEnableTargetCursor; }

	/** Returns the world location of the TDC cursor. */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	FVector GetTDCCursorWorldLocation() const;

	/** Returns the world location of the TDC cursor if enabled and valid. */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	bool TryGetTDCCursorWorldLocation(FVector& OutWorldLocation) const;

	/** Returns the shared display view settings. */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	FRadarDisplayView GetDisplayView() const { return DisplayView; }

	/** Returns the monotonically increasing revision index of the display view. */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	int32 GetDisplayViewRevision() const { return DisplayViewRevision; }

	/** Returns commanded azimuth in degrees relative to the plate; animated MSA does not apply this to detection. */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan Volume")
	FORCEINLINE float GetCurrentScanAzimuth() const { return CurrentScanAzimuth; }

	/** Returns commanded elevation in degrees, including STT/Spotlight steering. */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan Volume")
	FORCEINLINE float GetCurrentScanElevation() const { return CurrentScanElevation; }

	/** World pointing command for AnimBP; STT/Spotlight commands point at the designated target. */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan Volume")
	FVector GetCommandedBeamDirection() const;

	/** Returns the current active elevation bar index (0 to ElevationBars - 1). */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan Volume")
	FORCEINLINE int32 GetCurrentScanBar() const { return CurrentScanBar; }

	/** Returns true if the mechanical antenna sweep is traveling left-to-right. */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan Volume")
	FORCEINLINE bool IsScanningRight() const { return bScanningRight; }

	/** Retrieves normalized scan progress across bars and frame sweep. */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan Volume")
	void GetScanProgress(int32& OutBar, float& OutBarLevel, float& OutSweepLevel, bool& bOutScanningRight) const;

	/** Returns normalized scan position as (Azimuth%, Elevation%) for HUD/MFD sweeps. */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan Volume")
	FVector2D GetCurrentScanPosition() const;

	/** Evaluates whether an actor falls within the active antenna scan volume envelope. */
	UFUNCTION(BlueprintPure, Category = "Radar|Scan Volume")
	bool IsTargetInScanVolume(AActor* Target) const;

	/** Evaluates whether a direction vector in radar space falls within the active ACM sub-mode envelope. */
	bool IsDirectionInACMVolume(const FVector& LocalDirection, ERadarACMSubMode SubMode) const;

	/** Evaluates whether a target is terrain-masked by static geometry. */
	bool IsTerrainMasked(const FVector& RadarPosition, const FVector& TargetPosition, const AActor* TargetActor = nullptr, FHitResult* OutHit = nullptr) const;

	/** Returns true if in STT mode with an active tracked lock. */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	bool IsSTTLocked() const;

	/** Returns the actor currently locked in STT mode (or nullptr). */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	AActor* GetSTTLockedActor() const;

	/** Returns the track ID of the STT locked target (-1 if none). */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	FORCEINLINE int32 GetSTTLockedTrackID() const { return STTLockedTrackID; }

	/** Returns the track ID of the TWS bugged priority target (-1 if none). */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	FORCEINLINE int32 GetBuggedTrackID() const { return BuggedTrackID; }

	/** Returns the currently selected track (either STT locked or bugged in TWS). */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	bool GetSelectedTrack(FRadarTrack& OutTrack) const;

	/** Returns the actor of the currently selected track. */
	UFUNCTION(BlueprintPure, Category = "Radar|Lock")
	AActor* GetSelectedTargetActor() const;

	/** Clears target designation or breaks active STT lock (traditionally Stick TMS Down / Undesignate). */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock")
	void UndesignateTarget();

	/** Cycles target designation sequentially between active tracks (traditionally Stick TMS Right / Target Step). */
	UFUNCTION(BlueprintCallable, Category = "Radar|Lock")
	bool CycleTargetDesignation(bool bForward = true);

	/** Computes target aspect angle in degrees (-180° to +180°, 0° = tail-on, 180° = head-on). */
	UFUNCTION(BlueprintPure, Category = "Radar|Display & Cursor")
	float GetTrackAspectAngle(int32 TrackID) const;

	/** Calculates horizontal compass heading (0° to 360°) from a 3D velocity vector. */
	UFUNCTION(BlueprintPure, Category = "Radar|Kinematics")
	static float CalculateHeadingFromVelocity(const FVector& InVelocity, float FallbackHeading = 0.0f, float MinSpeedCmPerSec = 100.0f);

	/** Returns all local radar track files. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	const TArray<FRadarTrack>& GetAllTracks() const { return Tracks; }

	/** Returns tracks filtered by status. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	void GetTracksByStatus(ERadarTrackStatus InStatus, TArray<FRadarTrack>& OutTracks) const;

	/** Retrieves a specific track file by TrackID. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	bool GetTrackByID(int32 TrackID, FRadarTrack& OutTrack) const;

	/** Retrieves a track file associated with a specific target actor. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	bool GetTrackByActor(const AActor* TargetActor, FRadarTrack& OutTrack) const;

	/** Returns the actor of the closest contact. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	AActor* GetClosestContact() const;

	/** Returns the bearing to a specific track in degrees. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	float GetBearingToTrack(int32 TrackID) const;

	/** Returns the slant range to a specific track in cm. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	float GetRangeToTrack(int32 TrackID) const;

	/** Returns the closure rate to a specific track in cm/s. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	float GetClosureRateToTrack(int32 TrackID) const;

	/** Returns the total number of active local track files. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	FORCEINLINE int32 GetContactCount() const { return Tracks.Num(); }

	/** Returns the track that represents the highest threat. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	bool GetHighestThreatTrack(FRadarTrack& OutTrack) const;

	/** Dead-reckoned world position for display rendering without querying live actor. */
	UFUNCTION(BlueprintPure, Category = "Radar|Tracks")
	bool GetTrackDisplayWorldPosition(int32 TrackID, FVector& OutWorldPosition) const;

	// ========================================================================
	// Spotlight SAR & Terrain Staring Getters
	// ========================================================================

	/** Returns the designated Spotlight SAR world coordinate. */
	UFUNCTION(BlueprintPure, Category = "Radar|Spotlight SAR")
	FORCEINLINE FVector GetSpotlightTargetLocation() const { return SpotlightTargetLocation; }

	/** Returns current SAR dwell integration progress [0.0, 1.0]. */
	UFUNCTION(BlueprintPure, Category = "Radar|Spotlight SAR")
	FORCEINLINE float GetSpotlightDwellProgress() const { return SpotlightDwellProgress; }

	/** Returns squint angle in degrees between flight path and ground spotlight target. */
	UFUNCTION(BlueprintPure, Category = "Radar|Spotlight SAR")
	FORCEINLINE float GetSpotlightSquintAngle() const { return SpotlightSquintAngle; }

	/** Returns true if Spotlight SAR mode is active with an active ground lock. */
	UFUNCTION(BlueprintPure, Category = "Radar|Spotlight SAR")
	FORCEINLINE bool IsSpotlightActive() const { return RadarMode == ERadarOperatingMode::Spotlight && bHasSpotlightPoint; }

	/** Returns the helmet look direction in world space. */
	UFUNCTION(BlueprintPure, Category = "Radar|Air Combat Maneuver")
	FORCEINLINE FVector GetHelmetLookDirection() const { return HelmetLookDirection; }

	// ========================================================================
	// Radar Altimeter (RALT) Getters
	// ========================================================================

	/**
	 * Returns current radar altitude Above Ground Level (AGL) in centimeters.
	 * @param OutAltitudeCm Measured altitude above terrain in cm.
	 * @return True if a valid altimeter return is available within limits.
	 */
	UFUNCTION(BlueprintPure, Category = "Radar|Altimeter")
	bool GetRadarAltitude(float& OutAltitudeCm) const;

	/** Sets the socket name used as the radar altimeter antenna source. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Altimeter")
	void SetRadarAltimeterSocketName(FName InSocketName);

	/** Returns the socket name used as the radar altimeter antenna source. */
	UFUNCTION(BlueprintPure, Category = "Radar|Altimeter")
	FORCEINLINE FName GetRadarAltimeterSocketName() const { return RadarAltimeterSocketName; }

	/** Returns both world-space location and rotation of the radar altimeter antenna source. */
	UFUNCTION(BlueprintPure, Category = "Radar|Altimeter")
	void GetRadarAltimeterTransform(FVector& OutLocation, FRotator& OutRotation) const;

	// ========================================================================
	// Continuous Wave (CW) & Missile Guidance Illumination
	// ========================================================================

	/** Registers an in-flight missile requiring parent radar illumination (SARH, TVM, Command Guidance). */
	UFUNCTION(BlueprintCallable, Category = "Radar|Guidance")
	void RegisterGuidingMissile(URadarMissileGuidanceComponent* Missile);

	/** Unregisters a missile that has finished flight or terminated guidance. */
	UFUNCTION(BlueprintCallable, Category = "Radar|Guidance")
	void UnregisterGuidingMissile(URadarMissileGuidanceComponent* Missile);

	/** Returns true if transmitting Continuous Wave (CW) illumination towards TargetActor. */
	UFUNCTION(BlueprintPure, Category = "Radar|Guidance")
	bool IsContinuousWaveIlluminating(const AActor* TargetActor = nullptr) const;

	/** Returns the current continuous wave illuminated target actor (if active). */
	UFUNCTION(BlueprintPure, Category = "Radar|Guidance")
	AActor* GetContinuousWaveTarget() const { return ManualCWTargetActor.Get(); }

	/** Manually commands Continuous Wave (CW) illumination towards a target actor. */
	UFUNCTION(BlueprintCallable, Category = "Radar|Guidance")
	void SetContinuousWaveIllumination(AActor* TargetActor, bool bEnable);

	/** Returns all currently active guided missiles relying on this radar. */
	UFUNCTION(BlueprintPure, Category = "Radar|Guidance")
	TArray<URadarMissileGuidanceComponent*> GetActiveGuidingMissiles() const;

	void RegisterLaunchedRadarMissile(URadarMissileGuidanceComponent* Missile);
	void UnregisterLaunchedRadarMissile(URadarMissileGuidanceComponent* Missile);

	/** Returns statuses of all launched radar missiles currently supported. */
	UFUNCTION(BlueprintPure, Category = "Radar|Launched Missiles")
	void GetLaunchedRadarMissiles(TArray<FRadarLaunchedMissileStatus>& OutMissiles) const;

	// ========================================================================
	// Tactical Data Link Methods
	// ========================================================================

	/** Sets the radio network identifier (Server authority only). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Data Link")
	void SetDataLinkNetworkID(FName NewNetworkID);

	/** Enables or disables track transmission across the data link network. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Data Link")
	void SetDataLinkContributionEnabled(bool bEnabled);

	/** Enables or disables reception of remote data link tracks. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Data Link")
	void SetDataLinkReceptionEnabled(bool bEnabled);

	/** Returns true if actively connected and receiving data link network traffic. */
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	bool IsDataLinkConnected() const;

	/** Returns the participant network ID assigned by the Data Link Subsystem. */
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	int32 GetDataLinkParticipantID() const { return DataLinkParticipantID; }

	/** Returns all external tracks received via Data Link. */
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	void GetLinkedTracks(TArray<FRadarTrack>& OutTracks) const { OutTracks = LinkedTracks; }

	/** Immediately drops linked tracks invalidated by a local IFF policy change. */
	void RefreshDataLinkEligibility();

	/** Returns correlated tracks combining local radar returns and remote data link reports. */
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	void GetDisplayTracks(TArray<FRadarTrack>& OutTracks) const;

	/** Retrieves the best fresh weapon support track for a target actor. */
	bool GetBestWeaponSupportTrackForActor(const AActor* Actor, FRadarTrack& OutTrack) const;

	/** Returns the currently selected ContactID. */
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	int32 GetSelectedContactID() const { return SelectedContactID; }

	/** Retrieves a linked track by its unique negative TrackID. */
	UFUNCTION(BlueprintPure, Category = "Radar|Data Link")
	bool GetLinkedTrackByID(int32 TrackID, FRadarTrack& OutTrack) const;

	/** Retrieves fresh linked track for an actor from the data link cache. */
	bool GetFreshLinkedTrackForActor(const AActor* Actor, int32 OriginID, FRadarTrack& OutTrack) const;

	/** Returns the originating radar component that donated a linked track. */
	UAircraftRadarComponent* GetLinkedTrackSource(int32 TrackID) const;

	bool GetSelectedLinkedTrackForActor(const AActor* Actor, FRadarTrack& OutTrack) const;
	void ClearLinkedDesignation() { SelectedLinkedTrackID = -1; if (STTLockedTrackID < 0 && BuggedTrackID < 0) SelectedContactID = 0; }
	int32 GetSelectedLinkedTrackID() const { return SelectedLinkedTrackID; }

	bool CanDataLinkTransmit() const;
	bool CanDataLinkReceive() const;
	bool CanDataLinkRelay() const;

	/** Inhibits radio transmission or reception (jammer hook). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Data Link")
	void SetDataLinkRadioInhibited(bool bTransmitInhibited, bool bReceiveInhibited);

	void SetDataLinkParticipantID(int32 NewID) { DataLinkParticipantID = NewID; }
	void ReceiveDataLinkHeartbeat(float WorldTime);
	void ReceiveDataLinkReport(const FRadarTrack& Report, UAircraftRadarComponent* SourceRadar, float MeasurementTime, float WorldTime);

	// ========================================================================
	// IFF & Transponder Methods
	// ========================================================================

	/** Sets the radar platform's faction/team ID (Server authoritative). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|IFF")
	void SetTeamID(uint8 NewTeamID);

	/** Returns the assigned team ID. */
	UFUNCTION(BlueprintPure, Category = "Radar|IFF")
	uint8 GetTeamID() const { return TeamID; }

	/** Sets the radar platform's transponder squawk code. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|IFF")
	void SetSquawkCode(int32 NewSquawkCode);

	/** Returns the current squawk code. */
	UFUNCTION(BlueprintPure, Category = "Radar|IFF")
	int32 GetSquawkCode() const { return SquawkCode; }

	// ~Begin IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	// ~End IGenericTeamAgentInterface

	// ========================================================================
	// Multi-Crew Authorization
	// ========================================================================

	/** Grants radar control access to a crew member's player controller. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Multiplayer")
	bool GrantRadarAccess(APlayerController* Controller);

	/** Revokes radar control access from a player controller. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Multiplayer")
	void RevokeRadarAccess(APlayerController* Controller);

	/** Sets the currently active operator controlling radar inputs and cursor. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Radar|Multiplayer")
	bool SetActiveRadarOperator(APlayerController* Controller);

	/** Returns the local player's operator link channel (or null if unauthorized). */
	UFUNCTION(BlueprintPure, Category = "Radar|Multiplayer")
	ARadarOperatorLink* GetLocalOperatorLink() const { return LocalOperatorLink.Get(); }

	void RegisterLocalOperatorLink(ARadarOperatorLink* Link);
	void UnregisterLocalOperatorLink(ARadarOperatorLink* Link);
	void ApplyOperatorSnapshot(const FRadarOperatorSnapshot& Snapshot);
	void ApplyOperatorEvent(const FRadarOperatorEvent& Event);
	bool IsAuthorizedOperatorLink(const ARadarOperatorLink* Link) const;

	bool ExecuteOperatorCommand(ERadarCommandType Command, int32 IntValue, float ValueA, float ValueB,
		const FVector& WorldValue, const FRadarCursorState& Cursor, ERadarDisplayGeometry Geometry,
		int32& OutTrackID, int32 ViewRevision = -1);

	// Physics / Radar equation helpers
	float CalculateDetectionRange(float TargetRCS) const;
	float ResolveTargetRCS(const AActor* Candidate, bool& bOutFoundTag, FString& OutParsedTag) const;
	float CalculateEffectiveRCS(float BaseRCS, const AActor* TargetActor, const FVector& TargetToRadar) const;
	float CalculateAntennaBeamGain(const FVector& TargetPosition) const;
	float CalculateSignalStrength(float Range, float EffectiveRCS, float BeamGain) const;
	ERadarTargetDomain ResolveCandidateDomain(const AActor* Candidate) const;
	bool IsDomainAllowedForMode(ERadarTargetDomain Domain, ERadarOperatingMode Mode) const;

protected:
	virtual void PostLoad() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION() void OnRep_RadarMode();
	UFUNCTION() void OnRep_CurrentDisplayRange();
	UFUNCTION() void OnRep_TeamID();
	UFUNCTION() void OnRep_SquawkCode();

	// Scan & Detection
	void AdvanceAntennaSweep(float DeltaTime);
	FRadarBeamSample MakeBeamSample(float Azimuth, float Elevation, float SweepAzimuth = 0.0f, float SweepElevation = 0.0f) const;
	void SamplePhysicalPlateBeam(bool bIncludeMotion);
	bool UsesPhysicalPlateBeam() const { return ScanDrive == ERadarScanDrive::MSA && !bVirtuallySweepBeam; }
	bool IsWithinAntennaGimbal(float Bearing, float Elevation, float Margin = 1.0f) const;
	float CalculatePlateGain(const FVector& LocalTargetDir) const;
	void EmitScanProgress(int32 Bar, float BarLevel, float SweepLevel, bool bMovingRight);
	void GatherCandidateActors(const FVector& Origin, float Range, TArray<AActor*>& OutCandidates) const;
	void InvalidateSocketCaches();
	void PerformScanSweep(float DeltaTime);
	bool EvaluateCandidate(AActor* Candidate, FRadarTrack& OutTrack, FString* OutRejectReason = nullptr) const;
	bool CheckCandidateTags(const AActor* Candidate, FString* OutFoundTags = nullptr) const;
	ERadarIFFResult ClassifyIFF(const AActor* TargetActor) const;
	void DrawDebugScanVolume() const;

	// Track Management
	void UpdateTrackFiles(float DeltaTime);
	int32 CreateTrack(AActor* DetectedActor, const FRadarTrack& RawDetection);
	void UpdateTrack(int32 TrackIndex, const FRadarTrack& NewDetection);
	void PruneStaleTracks(float DeltaTime);
	void PerformSTTTracking();
	void PerformACMAcquisition();
	void PerformSpotlightTracking(float DeltaTime);
	bool ResolveAutoGroundIntersect(FVector& OutGroundLocation) const;
	void ComputeBearingElevation(const FVector& TargetPosition, float& OutBearing, float& OutElevation) const;
	FVector SmoothVelocity(const FVector& OldVelocity, const FVector& NewVelocity, float Alpha) const;
	void DrawDebugTracks() const;

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	TOptional<float> DisplayRangeBeforeEdit;
#endif

	/** Central unified command execution dispatcher. */
	bool ExecuteOrSubmitCommand(ERadarCommandType Command, int32 IntValue = 0, float ValueA = 0.0f,
		float ValueB = 0.0f, const FVector& WorldValue = FVector::ZeroVector,
		const FRadarCursorState& Cursor = FRadarCursorState(),
		ERadarDisplayGeometry Geometry = ERadarDisplayGeometry::BScope,
		bool bAuthoritative = true, APlayerController* Controller = nullptr);

	// Authoritative execution helpers (run on server / standalone)
	bool ExecuteAuthoritativeSetRadarMode(ERadarOperatingMode NewMode);
	bool ExecuteAuthoritativeSetDisplayRange(float NewRangeCm);
	bool ExecuteAuthoritativeCycleRangeScale(bool bIncrease = true);
	bool ExecuteAuthoritativeSetACMSubMode(ERadarACMSubMode NewSubMode);
	bool ExecuteAuthoritativeSetScanVolume(float InAzimuthWidth, float InElevationHeight, int32 InBars);
	bool ExecuteAuthoritativeApplyScanSizePreset(ERadarScanSize Preset);
	bool ExecuteAuthoritativeOffsetScanCenter(float AzimuthDelta, float ElevationDelta);
	bool ExecuteAuthoritativeDesignateSpotlightPoint(const FVector& WorldLocation);
	bool ExecuteAuthoritativeDesignateSpotlightActor(AActor* Actor);
	bool ExecuteAuthoritativeClearSpotlightTarget();
	bool ExecuteAuthoritativeSetHelmetLookDirection(const FVector& WorldDirection);
	bool ExecuteAuthoritativeLockTrack(int32 TrackID);
	bool ExecuteAuthoritativeBugTrack(int32 TrackID);
	bool ExecuteAuthoritativeBreakLock();
	bool ExecuteAuthoritativeClearBugTrack();
	bool ExecuteAuthoritativeMoveTDCCursor(FVector2D DeltaAxis);
	bool ExecuteAuthoritativeDesignateUnderCursor();
	bool ExecuteAuthoritativeDesignateLinkedTrack(int32 TrackID);

	// Internal state
	UPROPERTY(Transient) TArray<FRadarTrack> Tracks;
	UPROPERTY(Transient) TArray<FRadarTrack> LinkedTracks;
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
	float LastDataLinkReceptionTime = -1.0f;
	bool bDataLinkTransmitInhibited = false;
	bool bDataLinkReceiveInhibited = false;
	bool bClientDataLinkConnected = false;
	void PruneLinkedTracks(float DeltaTime);

	// Cached scan frame revisit time
	float CachedScanFrameTime = 5.0f;
	void RecalculateScanFrameTime();

	UPROPERTY(Transient) float CurrentScanAzimuth = 0.0f;
	UPROPERTY(Transient) float CurrentScanElevation = 0.0f;
	UPROPERTY(Transient) int32 CurrentScanBar = 0;
	UPROPERTY(Transient) bool bScanningRight = true;
	float ScanBarLevel = 0.0f;
	float ScanSweepLevel = 0.0f;
	uint32 ScanProgressRevision = 0;

	UPROPERTY(Transient) int32 STTLockedTrackID = -1;
	UPROPERTY(Transient) TObjectPtr<AActor> STTLockedActor = nullptr;
	UPROPERTY(Transient) int32 BuggedTrackID = -1;
	UPROPERTY(Transient) ERadarOperatingMode PreSTTMode = ERadarOperatingMode::Search;
	UPROPERTY(Transient) int32 NextTrackID = 1;

	float TrackUpdateAccumulator = 0.0f;
	float ScanSampleAccumulator = 0.0f;
	FQuat PreviousPlateRelativeRotation = FQuat::Identity;
	bool bHasPreviousPlateSample = false;
	int32 ElectronicBeamIndex = 0;

	TArray<FRadarBeamSample> ActiveSampleBeams;

	mutable TArray<TWeakObjectPtr<AActor>> CachedDiscoveryActors;
	mutable float LastCandidateDiscoveryTime = -1000.0f;
	mutable FVector LastCandidateDiscoveryOrigin = FVector::ZeroVector;
	mutable float LastCandidateDiscoveryRange = 0.0f;
	float LastCandidateDebugTime = -1000.0f;
	float LastRadarDebugTime = -1000.0f;

	UPROPERTY(Transient) TArray<TObjectPtr<ARadarOperatorLink>> OperatorLinks;
	UPROPERTY(Transient) TWeakObjectPtr<APlayerController> ActiveRadarOperator;
	UPROPERTY(Transient) TWeakObjectPtr<APlayerController> AutoAuthorizedController;
	UPROPERTY(Transient) TWeakObjectPtr<ARadarOperatorLink> LocalOperatorLink;

	float OperatorSnapshotAccumulator = 0.0f;
	int32 OperatorSnapshotRevision = 0;
	int32 DisplayViewRevision = 1;
	int32 OperatorEventSequence = 0;
	int32 LastAppliedOperatorEventSequence = 0;
	int32 ScanSweepCounter = 0;
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

	bool SetCursorFromDisplayPoint(const FVector2D& WidgetPosition, const FVector2D& WidgetSize);
	bool ResolveDisplayPointToCursor(const FVector2D& WidgetPosition,
		const FRadarDisplayProjection& Projection, FRadarCursorState& OutCursor) const;

	// Target designator cursor coordinates (transient runtime state)
	float TDCCursorAzimuth = 0.0f;
	float TDCCursorElevation = 0.0f;
	float TDCCursorRange = 3704000.0f;

	FVector SpotlightTargetLocation = FVector::ZeroVector;
	bool bHasSpotlightPoint = false;
	float SpotlightDwellProgress = 0.0f;
	float SpotlightSquintAngle = 0.0f;
	bool bSpotlightInBlindCone = false;
	bool bSpotlightGimbalExceeded = false;
	TWeakObjectPtr<AActor> SpotlightTrackedActor = nullptr;
	float SpotlightDwellAccumulator = 0.0f;
	FVector HelmetLookDirection = FVector::ZeroVector;

private:
	FORCEINLINE int32 FindTrackIndex(int32 TrackID) const
	{
		for (int32 i = 0; i < Tracks.Num(); ++i)
		{
			if (Tracks[i].TrackID == TrackID) return i;
		}
		return INDEX_NONE;
	}

	FORCEINLINE int32 FindTrackIndexByActor(const AActor* InActor) const
	{
		if (!InActor) return INDEX_NONE;
		for (int32 i = 0; i < Tracks.Num(); ++i)
		{
			if (Tracks[i].TrackedActor.Get() == InActor) return i;
		}
		return INDEX_NONE;
	}

	UPROPERTY(Transient) mutable TWeakObjectPtr<USceneComponent> CachedRadarSocketComponent = nullptr;
	mutable bool bRadarSocketResolved = false;

	UPROPERTY(Transient) mutable TWeakObjectPtr<USceneComponent> CachedAltimeterSocketComponent = nullptr;
	mutable bool bAltimeterSocketResolved = false;

	FVector GetRadarAltimeterLocation() const;
	FRotator GetRadarAltimeterRotation() const;
	void UpdateRadarAltimeter(float DeltaTime);

	float CachedRadarAltitude = 0.0f;
	bool bIsRadarAltitudeValid = false;
	float AltimeterUpdateAccumulator = 0.0f;

	UPROPERTY(Transient) TArray<TWeakObjectPtr<URadarMissileGuidanceComponent>> ActiveGuidingMissiles;
	struct FLaunchedMissileEntry
	{
		TWeakObjectPtr<URadarMissileGuidanceComponent> Missile;
		int32 ID = 0;
	};
	TArray<FLaunchedMissileEntry> LaunchedRadarMissiles;
	int32 NextLaunchedMissileID = 1;
	UPROPERTY(Transient) TArray<FRadarLaunchedMissileStatus> ClientLaunchedRadarMissiles;

	UPROPERTY(Transient) TWeakObjectPtr<AActor> ManualCWTargetActor = nullptr;
	bool bManualCWIlluminating = false;
};
