// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

//
// AircraftRadarComponent.cpp — Core radar logic, mode management, replication, and query helpers
//

#include "AircraftRadarComponent.h"
#include "RadarMissileGuidanceComponent.h"
#include "AircraftCombatSubsystem.h"
#include "AircraftCombatDebug.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "RadarOperatorLink.h"
#include "AircraftDataLinkSubsystem.h"
#include "Misc/DefaultValueHelper.h"

UAircraftRadarComponent::UAircraftRadarComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Read the completed skeletal pose when an AnimBP steers the radar plate.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	SetIsReplicatedByDefault(true);

	// Default range scale presets (in cm): 20nm, 40nm, 80nm, 160nm
	RangeScalePresets.Add(3704000.0f);   // 20 nm
	RangeScalePresets.Add(7408000.0f);   // 40 nm
	RangeScalePresets.Add(14816000.0f);  // 80 nm
	RangeScalePresets.Add(29632000.0f);  // 160 nm

	CurrentDisplayRange = 7408000.0f; // Default 40nm
}

void UAircraftRadarComponent::PostLoad()
{
	Super::PostLoad();

	if (ScanSizePreset != ERadarScanSize::Custom)
	{
		switch (ScanSizePreset)
		{
			case ERadarScanSize::Narrow_20: AzimuthScanWidth = 20.0f; break;
			case ERadarScanSize::Medium_40: AzimuthScanWidth = 40.0f; break;
			case ERadarScanSize::Wide_60:   AzimuthScanWidth = 60.0f; break;
			case ERadarScanSize::Full_120:  AzimuthScanWidth = 120.0f; break;
			default: break;
		}
	}
}

void UAircraftRadarComponent::BeginPlay()
{
	Super::BeginPlay();

	// Ensure AzimuthScanWidth reflects the configured preset if not using Custom
	if (ScanSizePreset != ERadarScanSize::Custom)
	{
		switch (ScanSizePreset)
		{
			case ERadarScanSize::Narrow_20: AzimuthScanWidth = 20.0f; break;
			case ERadarScanSize::Medium_40: AzimuthScanWidth = 40.0f; break;
			case ERadarScanSize::Wide_60:   AzimuthScanWidth = 60.0f; break;
			case ERadarScanSize::Full_120:  AzimuthScanWidth = 120.0f; break;
			default: break;
		}
	}

	SetComponentTickEnabled(true);
	Tracks.Reserve(32);
	InvalidateSocketCaches();
	CachedDiscoveryActors.Reset();
	RecalculateScanFrameTime();
	CurrentScanAzimuth = ScanCenterAzimuth - AzimuthScanWidth * 0.5f;
	CurrentScanElevation = ElevationBars > 1 ? ScanCenterElevation - ElevationScanHeight * 0.5f : ScanCenterElevation;

	if (bEnableTargetCursor)
	{
		TDCCursorRange = FMath::Clamp(TDCCursorRange, 1.0f, CurrentDisplayRange);
		const float DisplayCenter = MakeDisplayProjection(FVector2D::ZeroVector, FVector2D(1.0f, 1.0f)).ScanCenterAzimuth;
		TDCCursorAzimuth = FRotator::NormalizeAxis(DisplayCenter + FMath::Clamp(
			FMath::FindDeltaAngleDegrees(DisplayCenter, TDCCursorAzimuth),
			-AzimuthScanWidth * 0.5f, AzimuthScanWidth * 0.5f));
	}

	if (DisplayView.ActiveGeometry != ERadarDisplayGeometry::BScope &&
		DisplayView.ActiveGeometry != ERadarDisplayGeometry::PPI)
		DisplayView.ActiveGeometry = ERadarDisplayGeometry::BScope;
	if (!GetOwner() || GetOwner()->HasAuthority())
	{
		OnRadarDisplayRangeChanged.Broadcast(CurrentDisplayRange);
		if (bEnableTargetCursor) OnRadarCursorMoved.Broadcast(GetTDCCursorState());
	}

	if (UAircraftCombatSubsystem* Subsystem = UAircraftCombatSubsystem::Get(this))
	{
		Subsystem->RegisterRadar(this);
		if (AActor* OwnerActor = GetOwner())
		{
			Subsystem->RegisterCombatActor(OwnerActor);
		}
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UAircraftDataLinkSubsystem* DataLink = GetWorld()->GetSubsystem<UAircraftDataLinkSubsystem>())
			DataLink->RegisterRadar(this);
	}
}

void UAircraftRadarComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		if (UAircraftDataLinkSubsystem* DataLink = GetWorld()->GetSubsystem<UAircraftDataLinkSubsystem>())
			DataLink->UnregisterRadar(this);
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		for (ARadarOperatorLink* Link : OperatorLinks)
		{
			if (IsValid(Link)) Link->Destroy();
		}
	}
	OperatorLinks.Reset();
	LocalOperatorLink.Reset();
	if (UAircraftCombatSubsystem* Subsystem = UAircraftCombatSubsystem::Get(this))
	{
		Subsystem->UnregisterRadar(this);
	}

	if (UWorld* World = GetWorld())
	{
		FlushDebugStrings(World);
	}

	InvalidateSocketCaches();
	CachedDiscoveryActors.Reset();
	Tracks.Empty();
	LinkedTracks.Empty();
	LinkedTrackSources.Empty();
	LaunchedRadarMissiles.Reset();
	ClientLaunchedRadarMissiles.Reset();
	STTLockedTrackID = -1;
	STTLockedActor = nullptr;
	BuggedTrackID = -1;
	CachedRadarAltitude = 0.0f;
	bIsRadarAltitudeValid = false;
	Super::EndPlay(EndPlayReason);
}

void UAircraftRadarComponent::RecalculateScanFrameTime()
{
	float FrameTime = 5.0f;
	if (ScanDrive == ERadarScanDrive::MSA && ScanRateDegreesPerSecond > 0.0f)
	{
		FrameTime = AzimuthScanWidth * FMath::Max(1, ElevationBars) / ScanRateDegreesPerSecond;
	}
	else if (ScanDrive == ERadarScanDrive::PESA || ScanDrive == ERadarScanDrive::AESA)
	{
		const int32 AzCells = FMath::Max(1, FMath::CeilToInt(AzimuthScanWidth / FMath::Max(BeamAzimuthWidth, 1.0f)));
		const int32 Visits = ScanDrive == ERadarScanDrive::AESA ? FMath::Clamp(AESABeamsPerSample, 1, 32) : 1;
		FrameTime = FMath::CeilToFloat(static_cast<float>(AzCells * FMath::Max(1, ElevationBars)) / Visits) *
			FMath::Clamp(ScanSampleInterval, 0.016f, 1.0f);
	}
	CachedScanFrameTime = FMath::Max(0.1f, FrameTime);
}

void UAircraftRadarComponent::GetScanProgress(int32& OutBar, float& OutBarLevel,
	float& OutSweepLevel, bool& bOutScanningRight) const
{
	OutBar = CurrentScanBar;
	OutBarLevel = ScanBarLevel;
	OutSweepLevel = ScanSweepLevel;
	bOutScanningRight = bScanningRight;
}

void UAircraftRadarComponent::EmitScanProgress(int32 Bar, float BarLevel, float SweepLevel, bool bMovingRight)
{
	ScanBarLevel = FMath::Clamp(BarLevel, 0.0f, 1.0f);
	ScanSweepLevel = FMath::Clamp(SweepLevel, 0.0f, 1.0f);
	bScanningRight = bMovingRight;
	++ScanProgressRevision;
	OnScanProgressUpdated.Broadcast(Bar, ScanBarLevel, ScanSweepLevel, bScanningRight);
}

void UAircraftRadarComponent::AdvanceAntennaSweep(float DeltaTime)
{
	const float Width = FMath::Max(1.0f, AzimuthScanWidth);
	const float Left = ScanCenterAzimuth - Width * 0.5f;
	const float Right = ScanCenterAzimuth + Width * 0.5f;
	const int32 Bars = FMath::Clamp(ElevationBars, 1, 8);
	CurrentScanBar = FMath::Clamp(CurrentScanBar, 0, Bars - 1);
	CurrentScanAzimuth = FMath::Clamp(CurrentScanAzimuth, Left, Right);
	auto BarElevation = [&]()
	{
		return Bars > 1 ? ScanCenterElevation - ElevationScanHeight * 0.5f +
			ElevationScanHeight * CurrentScanBar / (Bars - 1) : ScanCenterElevation;
	};

	float Remaining = FMath::Max(0.0f, ScanRateDegreesPerSecond * DeltaTime);
	// Split at reversals/bar changes so samples never invent a diagonal beam across bars.
	int32 Segments = 0;
	do
	{
		const float OldAzimuth = CurrentScanAzimuth;
		const float Boundary = bScanningRight ? Right : Left;
		const float Travel = FMath::Min(Remaining, FMath::Abs(Boundary - OldAzimuth));
		CurrentScanAzimuth += bScanningRight ? Travel : -Travel;
		if (!UsesPhysicalPlateBeam())
		{
			ActiveSampleBeams.Add(MakeBeamSample((OldAzimuth + CurrentScanAzimuth) * 0.5f,
				BarElevation(), Travel * 0.5f));
		}
		Remaining -= Travel;
		if (FMath::IsNearlyEqual(CurrentScanAzimuth, Boundary))
		{
			bScanningRight = !bScanningRight;
			CurrentScanBar = (CurrentScanBar + 1) % Bars;
			if (CurrentScanBar == 0)
			{
				++ScanSweepCounter;
				OnScanSweepComplete.Broadcast();
			}
		}
	} while (Remaining > KINDA_SMALL_NUMBER && ++Segments < 128);

	CurrentScanElevation = BarElevation();
	const float BarLevel = FMath::Clamp((CurrentScanAzimuth - Left) / Width, 0.0f, 1.0f);
	const float TravelProgress = bScanningRight ? BarLevel : 1.0f - BarLevel;
	EmitScanProgress(CurrentScanBar, BarLevel, (CurrentScanBar + TravelProgress) / Bars, bScanningRight);
}

void UAircraftRadarComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* OwnerActor = GetOwner();
	const bool bHasAuthority = OwnerActor ? OwnerActor->HasAuthority() : true;
	const bool bIsEmitting = (RadarMode != ERadarOperatingMode::Off && RadarMode != ERadarOperatingMode::Standby);

	if (bHasAuthority)
	{
		PruneLinkedTracks(DeltaTime);
		RefreshAutomaticOperator();
		if (bEnableTargetCursor) for (ARadarOperatorLink* ActiveLink : OperatorLinks)
		{
			if (!IsValid(ActiveLink) || ActiveLink->GetOwner() != ActiveRadarOperator.Get()) continue;
			const FVector2D Input = ActiveLink->GetActiveCursorInput(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
			if (!Input.IsNearlyZero()) ExecuteAuthoritativeMoveTDCCursor(Input);
			break;
		}
		if (bIsEmitting)
		{
			if (UsesPhysicalPlateBeam() && RadarMode != ERadarOperatingMode::SingleTargetTrack &&
				RadarMode != ERadarOperatingMode::AirCombatManeuver && RadarMode != ERadarOperatingMode::Spotlight)
			{
				// Smooth commands for the next animation pose; detection below reads the completed current pose.
				AdvanceAntennaSweep(DeltaTime);
			}
			ScanSampleAccumulator += DeltaTime;
			const float Period = FMath::Clamp(ScanSampleInterval, 0.016f, 1.0f);
			int32 Samples = 0;
			while (ScanSampleAccumulator >= Period && Samples++ < 4)
			{
				ScanSampleAccumulator -= Period;
				switch (RadarMode)
				{
				case ERadarOperatingMode::SingleTargetTrack: PerformSTTTracking(); break;
				case ERadarOperatingMode::AirCombatManeuver: PerformACMAcquisition(); break;
				case ERadarOperatingMode::Spotlight: PerformSpotlightTracking(Period); break;
				default: PerformScanSweep(Period); break;
				}
			}
			if (ScanSampleAccumulator >= Period)
			{
				ScanSampleAccumulator = FMath::Fmod(ScanSampleAccumulator, Period);
			}

			// Update track files (aging, pruning, velocity smoothing) batched to TrackUpdateInterval
			TrackUpdateAccumulator += DeltaTime;
			if (TrackUpdateAccumulator >= TrackUpdateInterval)
			{
				UpdateTrackFiles(TrackUpdateAccumulator);
				PruneStaleTracks(TrackUpdateAccumulator);
				TrackUpdateAccumulator = 0.0f;
			}
		}
		else
		{
			TrackUpdateAccumulator += DeltaTime;
			if (TrackUpdateAccumulator >= TrackUpdateInterval)
			{
				PruneStaleTracks(TrackUpdateAccumulator);
				TrackUpdateAccumulator = 0.0f;
			}
		}
		RefreshCorrelatedSelection();
		OperatorSnapshotAccumulator += DeltaTime;
		if (OperatorSnapshotAccumulator >= FMath::Max(0.01f, OperatorSnapshotIntervalSeconds))
		{
			OperatorSnapshotAccumulator = 0.0f;
			PruneLaunchedRadarMissiles();
			PublishOperatorSnapshot();
		}
	}
	// Radar Altimeter (operates continuously and independently of main nose radar EMCON/emission state)
	if (bEnableRadarAltimeter)
	{
		if (bHasAuthority)
		{
			UpdateRadarAltimeter(DeltaTime);
		}
	}

	// Debug rendering & diagnostics — run if bEnableDebugTraces is enabled
	if (bEnableDebugTraces)
	{
		const float DebugNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		const bool bDebugIntervalElapsed = DebugNow - LastRadarDebugTime >= FAircraftCombatDebug::RadarDebugRefreshSeconds;
		if (bDebugIntervalElapsed) LastRadarDebugTime = DebugNow;
		bool bCanRenderDebug = true;
		if (bDebugOnlyPlayerControlled)
		{
			const APawn* PawnOwner = Cast<APawn>(GetOwner());
			bCanRenderDebug = PawnOwner ? PawnOwner->IsLocallyControlled() : true;
		}

		if (bCanRenderDebug && bDebugIntervalElapsed)
		{
			if (bDebugReferenceAxes && GetWorld())
			{
				FVector SourceLocation, ReferenceLocation;
				FRotator SourceRotation, ReferenceRotation;
				GetRadarSourceTransform(SourceLocation, SourceRotation);
				GetRadarDisplayReferenceTransform(ReferenceLocation, ReferenceRotation);
				DrawDebugCoordinateSystem(GetWorld(), SourceLocation, SourceRotation, 10000.0f,
					false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);
				DrawDebugCoordinateSystem(GetWorld(), ReferenceLocation, ReferenceRotation, 6000.0f,
					false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.0f);
				DrawDebugString(GetWorld(), SourceLocation + FVector(0, 0, 2000.0f),
					FString::Printf(TEXT("%s SOURCE"), *GetNameSafe(GetOwner())), nullptr,
					FColor::Cyan, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, false);
				DrawDebugString(GetWorld(), ReferenceLocation + FVector(0, 0, 1000.0f),
					FString::Printf(TEXT("%s DISPLAY FRAME"), *GetNameSafe(GetOwner())), nullptr,
					FColor::Yellow, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, false);
			}
			if ((bDrawScanVolume || bDrawAntennaBeam) && bIsEmitting)
			{
				DrawDebugScanVolume();
			}

			if (bDrawTrackSymbology)
			{
				DrawDebugTracks();
			}
			if (bEnableTargetCursor && bDebugCursor && GetWorld())
			{
				const FVector Origin = GetRadarLocation();
				const FVector CursorWorld = GetTDCCursorWorldLocation();
				const FVector CursorRay = CursorWorld - Origin;
				const FVector VisualEnd = Origin + CursorRay.GetSafeNormal() * FMath::Min(CursorRay.Size(), 300000.0f);
				DrawDebugLine(GetWorld(), Origin, VisualEnd, FColor::Magenta, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 2.0f);
				DrawDebugSphere(GetWorld(), VisualEnd, 500.0f, 8, FColor::Magenta, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds);
				const FRadarDisplayProjection BScope = MakeDisplayProjection(FVector2D::ZeroVector, FVector2D(512.0f, 512.0f));
				FVector2D CursorPixels;
				const bool bOnDisplay = FRadarDisplayGeometryMath::Project(CursorWorld, BScope, CursorPixels);
				DrawDebugString(GetWorld(), Origin + FVector(0, 0, 350.0f),
					FString::Printf(TEXT("%s TDC Az %+.1f El %+.1f R %.1f km | B-scope %.0f,%.0f %s"),
						*GetNameSafe(GetOwner()), TDCCursorAzimuth, TDCCursorElevation,
						TDCCursorRange / 100000.0f, CursorPixels.X, CursorPixels.Y,
						bOnDisplay ? TEXT("VISIBLE") : TEXT("OUTSIDE")),
					nullptr, bOnDisplay ? FColor::Magenta : FColor::Red, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, false);
			}
		}

		if (bEnableDiagnosticHUD && bDebugIntervalElapsed)
		{
			const UEnum* ModeEnum = StaticEnum<ERadarOperatingMode>();
			const FString ModeName = ModeEnum ? ModeEnum->GetDisplayNameTextByValue(static_cast<int64>(RadarMode)).ToString() : TEXT("Unknown");

			if (!bIsEmitting)
			{
				FAircraftCombatDebug::PrintRadarTelemetry(this, 0,
					FString::Printf(TEXT("[RADAR] STATUS: %s (EMCON / SILENT)"), *ModeName),
					FColor::Orange);
			}
			else
			{
				FAircraftCombatDebug::PrintRadarTelemetry(this, 0,
					FString::Printf(TEXT("=== [RADAR] Mode: %s | Tracks: %d | STT: %s | Bugged: %s ==="),
						*ModeName, Tracks.Num(),
						IsSTTLocked() ? *FString::Printf(TEXT("Track #%d"), STTLockedTrackID) : TEXT("NONE"),
						BuggedTrackID != -1 ? *FString::Printf(TEXT("Track #%d"), BuggedTrackID) : TEXT("NONE")),
					FColor::Cyan);

				const bool bIsAESA = ScanDrive == ERadarScanDrive::AESA;

				if (RadarMode == ERadarOperatingMode::AirCombatManeuver)
				{
					const UEnum* SubModeEnum = StaticEnum<ERadarACMSubMode>();
					const FString SubModeName = SubModeEnum ? SubModeEnum->GetDisplayNameTextByValue(static_cast<int64>(ACMSubMode)).ToString() : TEXT("Unknown");
					if (ACMSubMode == ERadarACMSubMode::HelmetCue)
					{
						const FString CueState = !HelmetLookDirection.IsNearlyZero() ? TEXT("SLAVED") : TEXT("BORESIGHT (NO CUE)");
						FAircraftCombatDebug::PrintRadarTelemetry(this, 1,
							FString::Printf(TEXT("ACM Auto-Acquire: HelmetCue [%s] | Range: %.1f km"),
								*CueState, ACMAutoLockRange / 100000.0f),
							!HelmetLookDirection.IsNearlyZero() ? FColor::Green : FColor::Yellow);
					}
					else
					{
						FAircraftCombatDebug::PrintRadarTelemetry(this, 1,
							FString::Printf(TEXT("ACM Auto-Acquire: %s | Range: %.1f km"),
								*SubModeName, ACMAutoLockRange / 100000.0f),
							FColor::Yellow);
					}
				}
				else if (RadarMode == ERadarOperatingMode::Spotlight)
				{
					const float SlantRangeKm = bHasSpotlightPoint ? FVector::Dist(GetRadarLocation(), SpotlightTargetLocation) / 100000.0f : 0.0f;
					const FString StatusStr = bSpotlightGimbalExceeded ? TEXT("GIMBAL LIMIT") : (bSpotlightInBlindCone ? TEXT("BLIND CONE") : (SpotlightDwellProgress >= 1.0f ? TEXT("IMAGE READY") : TEXT("INTEGRATING")));
					const FColor StatusColor = (bSpotlightGimbalExceeded || bSpotlightInBlindCone) ? FColor::Red : (SpotlightDwellProgress >= 1.0f ? FColor::Green : FColor::Yellow);

					FAircraftCombatDebug::PrintRadarTelemetry(this, 1,
						FString::Printf(TEXT("SAR Spotlight: Dwell %.0f%% | Squint %.0f° | Patch %.1fkm | Range %.1fkm [%s]"),
							SpotlightDwellProgress * 100.0f, SpotlightSquintAngle, SpotlightPatchRadius / 100000.0f, SlantRangeKm, *StatusStr),
						StatusColor);
				}
				else if (bIsAESA)
				{
					FAircraftCombatDebug::PrintRadarTelemetry(this, 1,
						FString::Printf(TEXT("AESA: %d beams/sample | %.0f° Az x %.0f° El"),
							AESABeamsPerSample, AzimuthScanWidth, ElevationScanHeight),
						FColor::Green);
				}
				else if (ScanDrive == ERadarScanDrive::PESA)
				{
					FAircraftCombatDebug::PrintRadarTelemetry(this, 1,
						FString::Printf(TEXT("PESA: one beam/sample | Az %+.1f° | Bar %d/%d"),
							CurrentScanAzimuth, CurrentScanBar + 1, ElevationBars), FColor::Green);
				}

				else
				{
					const float ElevationStep = (ElevationBars > 1) ? ElevationScanHeight / static_cast<float>(ElevationBars - 1) : 0.0f;
					const float BarElevation = ScanCenterElevation - (ElevationScanHeight * 0.5f) + (ElevationStep * CurrentScanBar);

					FAircraftCombatDebug::PrintRadarTelemetry(this, 1,
						FString::Printf(TEXT("MSA: Az %+.1f° (±%.0f°) | Bar %d/%d (El %+.1f°) | %s | %s"),
							CurrentScanAzimuth, AzimuthScanWidth * 0.5f,
							CurrentScanBar + 1, ElevationBars, BarElevation,
							bScanningRight ? TEXT("RIGHT ->") : TEXT("<- LEFT"),
							UsesPhysicalPlateBeam() ? TEXT("ANIMATED PLATE") : TEXT("VIRTUAL SWEEP")),
						FColor::Yellow);
				}

				FAircraftCombatDebug::PrintRadarTelemetry(this, 2,
					FString::Printf(TEXT("Range Scale: %.1f km | Max Instrumented: %.1f km"),
						CurrentDisplayRange / 100000.0f, MaxDetectionRange / 100000.0f),
					FColor::White);
			}

			if (bEnableRadarAltimeter)
			{
				float RAltCm = 0.0f;
				if (GetRadarAltitude(RAltCm))
				{
					const float RAltFt = RAltCm / 30.48f;
					FAircraftCombatDebug::PrintRadarTelemetry(this, 4,
						FString::Printf(TEXT("RALT: %.0f ft (%.1f m) [LOCKED]"), RAltFt, RAltCm / 100.0f),
						FColor::Green);
				}
				else
				{
					FAircraftCombatDebug::PrintRadarTelemetry(this, 4,
						TEXT("RALT: --- ft [OFF / OUT OF LIMITS]"),
						FColor::Red);
				}
			}
		}
	}
}

void UAircraftRadarComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAircraftRadarComponent, RadarMode);
	DOREPLIFETIME(UAircraftRadarComponent, CurrentDisplayRange);
	DOREPLIFETIME(UAircraftRadarComponent, TeamID);
	DOREPLIFETIME(UAircraftRadarComponent, SquawkCode);
}

void UAircraftRadarComponent::SetTeamID(uint8 NewTeamID)
{
	if (TeamID == NewTeamID)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (OwnerActor && !OwnerActor->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("UAircraftRadarComponent::SetTeamID called on non-authority actor %s. Must be set on server."), *OwnerActor->GetName());
		return;
	}

	TeamID = NewTeamID;
	OnTeamChanged.Broadcast(TeamID);
}

void UAircraftRadarComponent::OnRep_TeamID()
{
	OnTeamChanged.Broadcast(TeamID);
}

void UAircraftRadarComponent::SetSquawkCode(int32 NewSquawkCode)
{
	if (SquawkCode == NewSquawkCode)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (OwnerActor && !OwnerActor->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("UAircraftRadarComponent::SetSquawkCode called on non-authority actor %s. Must be set on server."), *OwnerActor->GetName());
		return;
	}

	SquawkCode = NewSquawkCode;
}

void UAircraftRadarComponent::OnRep_SquawkCode()
{
}

void UAircraftRadarComponent::SetGenericTeamId(const FGenericTeamId& InTeamID)
{
	SetTeamID(InTeamID.GetId());
}

FGenericTeamId UAircraftRadarComponent::GetGenericTeamId() const
{
	return FGenericTeamId(TeamID);
}

ETeamAttitude::Type UAircraftRadarComponent::GetTeamAttitudeTowards(const AActor& Other) const
{
	const IGenericTeamAgentInterface* OtherAgent = FCombatTeamUtility::ResolveTeamAgent(&Other);
	if (!OtherAgent)
	{
		return FCombatTeamUtility::UnknownAttitudeToTeamAttitude(UnknownContactAttitude);
	}

	const FGenericTeamId OtherTeam = OtherAgent->GetGenericTeamId();
	if (OtherTeam == FGenericTeamId::NoTeam || TeamID == FGenericTeamId::NoTeam)
	{
		return ETeamAttitude::Neutral;
	}

	return (OtherTeam.GetId() == TeamID) ? ETeamAttitude::Friendly : ETeamAttitude::Hostile;
}

void UAircraftRadarComponent::OnRep_RadarMode()
{
	// The owner-only coherent snapshot dispatches this event after related track state is applied.
}

bool UAircraftRadarComponent::ExecuteAuthoritativeSetRadarMode(ERadarOperatingMode NewMode)
{
	if (RadarMode == NewMode)
	{
		return true;
	}

	const ERadarOperatingMode OldMode = RadarMode;
	AActor* PreviousLockedActor = STTLockedActor;

	// If leaving STT, clear the lock and restore track status
	if (OldMode == ERadarOperatingMode::SingleTargetTrack && NewMode != ERadarOperatingMode::SingleTargetTrack)
	{
		STTLockedActor = nullptr;
		if (STTLockedTrackID >= 0)
		{
			const int32 OldLockID = STTLockedTrackID;
			const int32 TrackIndex = FindTrackIndex(STTLockedTrackID);
			const bool bWasBugged = (BuggedTrackID >= 0 && BuggedTrackID == OldLockID);

			if (TrackIndex != INDEX_NONE)
			{
				if (bWasBugged)
				{
					Tracks[TrackIndex].Status = ERadarTrackStatus::Bugged;
					Tracks[TrackIndex].bIsBeamTarget = true;
					Tracks[TrackIndex].bIsBugged = true;
				}
				else if (NewMode == ERadarOperatingMode::TrackWhileScan)
				{
					Tracks[TrackIndex].Status = ERadarTrackStatus::Tracked;
					Tracks[TrackIndex].bIsBeamTarget = false;
					Tracks[TrackIndex].bIsBugged = false;
				}
				else
				{
					Tracks[TrackIndex].Status = ERadarTrackStatus::Search;
					Tracks[TrackIndex].bIsBeamTarget = false;
					Tracks[TrackIndex].bIsBugged = false;
				}
			}

			STTLockedTrackID = -1;
			RecordOperatorEvent(ERadarOperatorEventType::LockLost, OldLockID);
			OnRadarLockLost.Broadcast(OldLockID);
			if (!bWasBugged)
			{
				OnRadarTrackDeselected.Broadcast(OldLockID);
			}
			else if (TrackIndex != INDEX_NONE && NewMode != ERadarOperatingMode::Off &&
				NewMode != ERadarOperatingMode::Standby)
			{
				OnRadarTrackSelected.Broadcast(Tracks[TrackIndex]);
			}
		}
	}

	// Store pre-STT mode for BreakLock
	if (NewMode == ERadarOperatingMode::SingleTargetTrack && OldMode != ERadarOperatingMode::SingleTargetTrack)
	{
		PreSTTMode = OldMode;
	}

	// If switching to Off or Standby, flush all track files and clear locks
	if (NewMode == ERadarOperatingMode::Off || NewMode == ERadarOperatingMode::Standby)
	{
		const bool bHadContacts = !Tracks.IsEmpty();
		if (BuggedTrackID >= 0) OnRadarTrackDeselected.Broadcast(BuggedTrackID);
		for (const FRadarTrack& Track : Tracks) OnRadarContactLost.Broadcast(Track.TrackID);
		Tracks.Empty();
		STTLockedTrackID = -1;
		BuggedTrackID = -1;
		if (bHadContacts) OnRadarAllContactsCleared.Broadcast();
	}

	// If entering Spotlight, initialize dwell and resolve ground point if needed
	if (NewMode == ERadarOperatingMode::Spotlight)
	{
		SpotlightDwellAccumulator = 0.0f;
		SpotlightDwellProgress = 0.0f;

		// If no ground point is set, try to inherit STT target or run auto-intersect
		if (!bHasSpotlightPoint)
		{
			if (IsValid(PreviousLockedActor))
			{
				ExecuteAuthoritativeDesignateSpotlightActor(PreviousLockedActor);
			}
			else if (bSpotlightAutoGroundIntersect)
			{
				FVector AutoPoint;
				if (ResolveAutoGroundIntersect(AutoPoint))
				{
					ExecuteAuthoritativeDesignateSpotlightPoint(AutoPoint);
				}
			}
		}
	}
	else if (OldMode == ERadarOperatingMode::Spotlight)
	{
		SpotlightDwellAccumulator = 0.0f;
		SpotlightDwellProgress = 0.0f;
	}

	RadarMode = NewMode;
	ScanSampleAccumulator = 0.0f;
	ActiveSampleBeams.Reset();
	bHasPreviousPlateSample = false;
	ElectronicBeamIndex = 0;

	ScanBarLevel = 0.0f;
	ScanSweepLevel = 0.0f;

	// Keep tick enabled so diagnostics and standby warnings function
	SetComponentTickEnabled(true);

	// Reset scan position when changing modes (except STT and Spotlight which slave directly to target)
	if (NewMode != ERadarOperatingMode::SingleTargetTrack && NewMode != ERadarOperatingMode::Spotlight)
	{
		CurrentScanAzimuth = ScanCenterAzimuth - (AzimuthScanWidth * 0.5f);
		CurrentScanElevation = ElevationBars > 1 ? ScanCenterElevation - ElevationScanHeight * 0.5f : ScanCenterElevation;
		bScanningRight = true;
		CurrentScanBar = 0;
	}

	OnRadarModeChanged.Broadcast(RadarMode);
	return true;
}

void UAircraftRadarComponent::InvalidateSocketCaches()
{
	CachedRadarSocketComponent = nullptr;
	bRadarSocketResolved = false;
	CachedAltimeterSocketComponent = nullptr;
	bAltimeterSocketResolved = false;
}

void UAircraftRadarComponent::SetRadarSocketName(FName InSocketName)
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
		if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority()) return;
	RadarSocketName = InSocketName;
	CachedRadarSocketComponent = nullptr;
	bRadarSocketResolved = false;
	bHasPreviousPlateSample = false;
	ActiveSampleBeams.Reset();
}

void UAircraftRadarComponent::SetRadarAltimeterSocketName(FName InSocketName)
{
	if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority()) return;
	RadarAltimeterSocketName = InSocketName;
	CachedAltimeterSocketComponent = nullptr;
	bAltimeterSocketResolved = false;
}

void UAircraftRadarComponent::GetRadarSourceTransform(FVector& OutLocation, FRotator& OutRotation) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		OutLocation = FVector::ZeroVector;
		OutRotation = FRotator::ZeroRotator;
		return;
	}

	bool bResolvedTransform = false;

	// Resolve and cache radar socket transform
	if (RadarSocketName != NAME_None)
	{
		if (bRadarSocketResolved && CachedRadarSocketComponent.IsValid() &&
			!CachedRadarSocketComponent->DoesSocketExist(RadarSocketName))
		{
			bRadarSocketResolved = false;
			CachedRadarSocketComponent = nullptr;
		}
		if (bRadarSocketResolved)
		{
			if (CachedRadarSocketComponent.IsValid())
			{
				OutLocation = CachedRadarSocketComponent->GetSocketLocation(RadarSocketName);
				OutRotation = CachedRadarSocketComponent->GetSocketRotation(RadarSocketName);
				bResolvedTransform = true;
			}
		}
		else
		{
			bRadarSocketResolved = true;
			CachedRadarSocketComponent = nullptr;

			// Check RootComponent first
			if (USceneComponent* RootComp = OwnerActor->GetRootComponent())
			{
				if (RootComp->DoesSocketExist(RadarSocketName))
				{
					CachedRadarSocketComponent = RootComp;
					OutLocation = RootComp->GetSocketLocation(RadarSocketName);
					OutRotation = RootComp->GetSocketRotation(RadarSocketName);
					bResolvedTransform = true;
				}
			}

			// Search mesh components on owner actor
			if (!bResolvedTransform)
			{
				TInlineComponentArray<UMeshComponent*> MeshComponents(OwnerActor);
				for (UMeshComponent* MeshComp : MeshComponents)
				{
					if (MeshComp && MeshComp->DoesSocketExist(RadarSocketName))
					{
						CachedRadarSocketComponent = MeshComp;
						OutLocation = MeshComp->GetSocketLocation(RadarSocketName);
						OutRotation = MeshComp->GetSocketRotation(RadarSocketName);
						bResolvedTransform = true;
						break;
					}
				}
			}

			// Search all scene components on owner actor
			if (!bResolvedTransform)
			{
				TInlineComponentArray<USceneComponent*> SceneComponents(OwnerActor);
				for (USceneComponent* SceneComp : SceneComponents)
				{
					if (SceneComp && SceneComp->DoesSocketExist(RadarSocketName))
					{
						CachedRadarSocketComponent = SceneComp;
						OutLocation = SceneComp->GetSocketLocation(RadarSocketName);
						OutRotation = SceneComp->GetSocketRotation(RadarSocketName);
						bResolvedTransform = true;
						break;
					}
				}
			}

		}
	}

	// 2. Fallback: Socket is empty (NAME_None) or does not exist on any component, use root (0, 0, 0)
	if (!bResolvedTransform)
	{
		if (const USceneComponent* RootComp = OwnerActor->GetRootComponent())
		{
			OutLocation = RootComp->GetComponentLocation();
			OutRotation = RootComp->GetComponentRotation();
		}
		else
		{
			OutLocation = OwnerActor->GetActorLocation();
			OutRotation = OwnerActor->GetActorRotation();
		}
	}

	// 3. Apply optional local radar translation offset
	if (!RadarLocationOffset.IsNearlyZero())
	{
		OutLocation += OutRotation.RotateVector(RadarLocationOffset);
	}
}

FVector UAircraftRadarComponent::GetRadarLocation() const
{
	FVector Loc;
	FRotator Rot;
	GetRadarSourceTransform(Loc, Rot);
	return Loc;
}

FRotator UAircraftRadarComponent::GetRadarRotation() const
{
	FVector Loc;
	FRotator Rot;
	GetRadarSourceTransform(Loc, Rot);
	return Rot;
}

void UAircraftRadarComponent::GetRadarDisplayReferenceTransform(FVector& OutLocation, FRotator& OutRotation) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		OutLocation = FVector::ZeroVector;
		OutRotation = FRotator::ZeroRotator;
		return;
	}

	const USceneComponent* Reference = OwnerActor->GetRootComponent();
	if (RadarDisplayReferenceComponentName != NAME_None)
	{
		TInlineComponentArray<USceneComponent*> Components(OwnerActor);
		for (const USceneComponent* Component : Components)
		{
			if (Component && Component->GetFName() == RadarDisplayReferenceComponentName)
			{
				Reference = Component;
				break;
			}
		}
	}
	OutLocation = Reference ? Reference->GetComponentLocation() : OwnerActor->GetActorLocation();
	OutRotation = Reference ? Reference->GetComponentRotation() : OwnerActor->GetActorRotation();
}

FRadarDisplayProjection UAircraftRadarComponent::MakeDisplayProjection(const FVector2D& WidgetTopLeft,
	const FVector2D& WidgetSize, ERadarDisplayGeometry Geometry, bool bHeadingUp) const
{
	FRadarDisplayProjection Projection;
	Projection.Geometry = Geometry;
	Projection.RadarOrigin = GetRadarLocation();
	FVector ReferenceLocation;
	GetRadarDisplayReferenceTransform(ReferenceLocation, Projection.ReferenceRotation);
	Projection.WidgetTopLeft = WidgetTopLeft;
	Projection.WidgetSize = WidgetSize;
	Projection.DisplayRangeCm = CurrentDisplayRange;
	const FVector ScanCenterDirection = GetRadarRotation().RotateVector(
		FRotator(ScanCenterElevation, ScanCenterAzimuth, 0.0f).Vector());
	Projection.ScanCenterAzimuth = Projection.ReferenceRotation.UnrotateVector(ScanCenterDirection).Rotation().Yaw;
	Projection.AzimuthWidth = AzimuthScanWidth;
	Projection.CursorElevation = bEnableTargetCursor ? TDCCursorElevation : 0.0f;
	Projection.bHeadingUp = bHeadingUp;
	return Projection;
}

bool UAircraftRadarComponent::ProjectWorldToDisplay(const FVector& WorldLocation,
	FVector2D WidgetSize, FVector2D& OutWidgetPosition) const
{
	const bool bVisible = FRadarDisplayGeometryMath::Project(WorldLocation,
		MakeDisplayProjection(FVector2D::ZeroVector, WidgetSize,
			DisplayView.ActiveGeometry, DisplayView.bHeadingUp), OutWidgetPosition);
	if (WidgetSize.X > 0.0f && WidgetSize.Y > 0.0f)
	{
		OutWidgetPosition -= WidgetSize * 0.5f;
	}
	return bVisible;
}

void UAircraftRadarComponent::GetRadarAltimeterTransform(FVector& OutLocation, FRotator& OutRotation) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		OutLocation = FVector::ZeroVector;
		OutRotation = FRotator::ZeroRotator;
		return;
	}

	if (RadarAltimeterSocketName != NAME_None)
	{
		if (bAltimeterSocketResolved)
		{
			if (CachedAltimeterSocketComponent.IsValid())
			{
				OutLocation = CachedAltimeterSocketComponent->GetSocketLocation(RadarAltimeterSocketName);
				OutRotation = CachedAltimeterSocketComponent->GetSocketRotation(RadarAltimeterSocketName);
				return;
			}
		}
		else
		{
			bAltimeterSocketResolved = true;
			CachedAltimeterSocketComponent = nullptr;

			if (USceneComponent* RootComp = OwnerActor->GetRootComponent())
			{
				if (RootComp->DoesSocketExist(RadarAltimeterSocketName))
				{
					CachedAltimeterSocketComponent = RootComp;
					OutLocation = RootComp->GetSocketLocation(RadarAltimeterSocketName);
					OutRotation = RootComp->GetSocketRotation(RadarAltimeterSocketName);
					return;
				}
			}

			TInlineComponentArray<UMeshComponent*> MeshComponents(OwnerActor);
			for (UMeshComponent* MeshComp : MeshComponents)
			{
				if (MeshComp && MeshComp->DoesSocketExist(RadarAltimeterSocketName))
				{
					CachedAltimeterSocketComponent = MeshComp;
					OutLocation = MeshComp->GetSocketLocation(RadarAltimeterSocketName);
					OutRotation = MeshComp->GetSocketRotation(RadarAltimeterSocketName);
					return;
				}
			}

			TInlineComponentArray<USceneComponent*> SceneComponents(OwnerActor);
			for (USceneComponent* SceneComp : SceneComponents)
			{
				if (SceneComp && SceneComp->DoesSocketExist(RadarAltimeterSocketName))
				{
					CachedAltimeterSocketComponent = SceneComp;
					OutLocation = SceneComp->GetSocketLocation(RadarAltimeterSocketName);
					OutRotation = SceneComp->GetSocketRotation(RadarAltimeterSocketName);
					return;
				}
			}
		}
	}

	// Fallback to primary radar antenna transform
	GetRadarSourceTransform(OutLocation, OutRotation);
}

FVector UAircraftRadarComponent::GetRadarAltimeterLocation() const
{
	FVector Loc;
	FRotator Rot;
	GetRadarAltimeterTransform(Loc, Rot);
	return Loc;
}

FRotator UAircraftRadarComponent::GetRadarAltimeterRotation() const
{
	FVector Loc;
	FRotator Rot;
	GetRadarAltimeterTransform(Loc, Rot);
	return Rot;
}

void UAircraftRadarComponent::UpdateRadarAltimeter(float DeltaTime)
{
	if (!bEnableRadarAltimeter)
	{
		bIsRadarAltitudeValid = false;
		return;
	}

	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		bIsRadarAltitudeValid = false;
		return;
	}

	AltimeterUpdateAccumulator += DeltaTime;
	if (AltimeterUpdateInterval > 0.0f && AltimeterUpdateAccumulator < AltimeterUpdateInterval)
	{
		return;
	}
	const float TimeSinceLastUpdate = (AltimeterUpdateAccumulator > 0.0f) ? AltimeterUpdateAccumulator : DeltaTime;
	AltimeterUpdateAccumulator = 0.0f;

	// 1. Resolve altimeter transform and check attitude limits
	FVector AltimeterLocation;
	FRotator AltimeterRotation;
	GetRadarAltimeterTransform(AltimeterLocation, AltimeterRotation);

	const FVector AntennaDown = -AltimeterRotation.RotateVector(FVector::UpVector);
	const FVector WorldNadir = FVector(0.0f, 0.0f, -1.0f);

	const float CosTilt = FVector::DotProduct(AntennaDown, WorldNadir);
	const float MaxTiltCos = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(MaxAltimeterAttitudeAngle, 10.0f, 89.0f)));

	if (CosTilt < MaxTiltCos)
	{
		// Exceeded attitude limit (e.g. banked > 50° or inverted)
		bIsRadarAltitudeValid = false;
		return;
	}

	// 2. Query parameters & collision exclusion
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RadarAltimeterTrace), false, OwnerActor);
	QueryParams.AddIgnoredActor(OwnerActor);
	TArray<AActor*> AttachedActors;
	OwnerActor->GetAttachedActors(AttachedActors, true, true);
	QueryParams.AddIgnoredActors(AttachedActors);

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	// 3. First-Return Conical Trace
	// Real RALT measures shortest slant range within the beam cone to capture terrain obstacles.
	float MinSlantDistance = TNumericLimits<float>::Max();
	bool bFoundHit = false;

	// Central nadir ray
	const FVector NadirEnd = AltimeterLocation + (WorldNadir * MaxRadarAltitude);
	FHitResult NadirHit;
	if (World->LineTraceSingleByObjectType(NadirHit, AltimeterLocation, NadirEnd, ObjectQueryParams, QueryParams))
	{
		MinSlantDistance = NadirHit.Distance;
		bFoundHit = true;
	}

	// Peripheral conical rays (if enabled)
	if (bAltimeterConicalSampling)
	{
		const float HalfConeRad = FMath::DegreesToRadians(FMath::Clamp(AltimeterBeamwidthDegrees * 0.5f, 5.0f, 45.0f));
		const float TanHalf = FMath::Tan(HalfConeRad);
		const FVector AntennaForward = AltimeterRotation.RotateVector(FVector::ForwardVector);
		const FVector AntennaRight = AltimeterRotation.RotateVector(FVector::RightVector);

		const FVector PeripheralDirs[4] = {
			(AntennaDown + AntennaForward * TanHalf).GetSafeNormal(),
			(AntennaDown - AntennaForward * TanHalf).GetSafeNormal(),
			(AntennaDown + AntennaRight * TanHalf).GetSafeNormal(),
			(AntennaDown - AntennaRight * TanHalf).GetSafeNormal()
		};

		for (int32 i = 0; i < 4; ++i)
		{
			if (PeripheralDirs[i].Z < -0.05f)
			{
				const FVector RayEnd = AltimeterLocation + (PeripheralDirs[i] * MaxRadarAltitude);
				FHitResult PeriHit;
				if (World->LineTraceSingleByObjectType(PeriHit, AltimeterLocation, RayEnd, ObjectQueryParams, QueryParams))
				{
					if (PeriHit.Distance < MinSlantDistance)
					{
						MinSlantDistance = PeriHit.Distance;
					}
					bFoundHit = true;
				}
			}
		}
	}

	// 4. Update tracking loop smoothed altitude
	if (bFoundHit && MinSlantDistance <= MaxRadarAltitude)
	{
		if (!bIsRadarAltitudeValid)
		{
			CachedRadarAltitude = MinSlantDistance;
		}
		else
		{
			CachedRadarAltitude = FMath::FInterpTo(CachedRadarAltitude, MinSlantDistance, TimeSinceLastUpdate, AltimeterSmoothingSpeed);
		}
		bIsRadarAltitudeValid = true;
	}
	else
	{
		bIsRadarAltitudeValid = false;
	}
}

bool UAircraftRadarComponent::GetRadarAltitude(float& OutAltitudeCm) const
{
	if (bEnableRadarAltimeter && bIsRadarAltitudeValid)
	{
		OutAltitudeCm = CachedRadarAltitude;
		return true;
	}

	OutAltitudeCm = 0.0f;
	return false;
}

bool UAircraftRadarComponent::IsRadarEmitting() const
{
	return RadarMode != ERadarOperatingMode::Off && RadarMode != ERadarOperatingMode::Standby;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeSetACMSubMode(ERadarACMSubMode NewSubMode)
{
	ACMSubMode = NewSubMode;
	if (RadarMode != ERadarOperatingMode::AirCombatManeuver)
	{
		ExecuteAuthoritativeSetRadarMode(ERadarOperatingMode::AirCombatManeuver);
	}
	return true;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeSetHelmetLookDirection(const FVector& InWorldDirection)
{
	HelmetLookDirection = InWorldDirection.GetSafeNormal();
	return true;
}

ERadarTargetDomain UAircraftRadarComponent::ResolveCandidateDomain(const AActor* Candidate) const
{
	const UAircraftCombatSettings* Settings = UAircraftCombatSettings::Get();
	return Settings ? Settings->ResolveTargetDomain(Candidate, bSearchComponentTags) : ERadarTargetDomain::Air;
}

bool UAircraftRadarComponent::IsDomainAllowedForMode(ERadarTargetDomain Domain, ERadarOperatingMode Mode) const
{
	switch (Mode)
	{
		case ERadarOperatingMode::Search:
		case ERadarOperatingMode::TrackWhileScan:
		case ERadarOperatingMode::AirCombatManeuver:
		case ERadarOperatingMode::SingleTargetTrack:
			return AirModeAllowedDomains.IsEmpty() || AirModeAllowedDomains.Contains(Domain);

		case ERadarOperatingMode::GroundMapping:
			return GroundMappingAllowedDomains.IsEmpty() || GroundMappingAllowedDomains.Contains(Domain);

		case ERadarOperatingMode::SeaSearch:
			return SeaSearchAllowedDomains.IsEmpty() || SeaSearchAllowedDomains.Contains(Domain);

		case ERadarOperatingMode::Spotlight:
			return SpotlightAllowedDomains.IsEmpty() || SpotlightAllowedDomains.Contains(Domain);

		default:
			return true;
	}
}

bool UAircraftRadarComponent::IsDirectionInACMVolume(const FVector& LocalDirection, ERadarACMSubMode SubMode) const
{
	if (LocalDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector NormLocalDir = LocalDirection.GetSafeNormal();

	// Target must be in the forward hemisphere of the radar antenna (X > 0)
	if (NormLocalDir.X <= 0.0f)
	{
		return false;
	}

	const FRotator LocalRot = NormLocalDir.Rotation();
	const float AzimuthDeg = LocalRot.Yaw;
	const float ElevationDeg = LocalRot.Pitch;

	switch (SubMode)
	{
		case ERadarACMSubMode::VerticalScan:
		{
			const float HalfAzWidth = ACMVerticalScanAzimuthWidth * 0.5f;
			const bool bInAzimuth = FMath::Abs(AzimuthDeg) <= HalfAzWidth;
			const bool bInElevation = (ElevationDeg >= ACMVerticalScanMinElevation) && (ElevationDeg <= ACMVerticalScanMaxElevation);
			return bInAzimuth && bInElevation;
		}

		case ERadarACMSubMode::Boresight:
		{
			// Conical envelope around boresight (+X axis)
			const float CosAngle = FMath::Cos(FMath::DegreesToRadians(ACMBoresightConeAngle));
			return NormLocalDir.X >= CosAngle;
		}

		case ERadarACMSubMode::HelmetCue:
		{
			// If helmet look direction is set, test against local helmet direction
			if (!HelmetLookDirection.IsNearlyZero())
			{
				const FVector LocalHelmet = GetRadarRotation().UnrotateVector(HelmetLookDirection).GetSafeNormal();
				if (LocalHelmet.X > 0.0f)
				{
					const float CosAngle = FMath::Cos(FMath::DegreesToRadians(ACMBoresightConeAngle));
					return FVector::DotProduct(NormLocalDir, LocalHelmet) >= CosAngle;
				}
			}

			// Fallback to boresight (+X axis) if helmet direction is unset or pointed behind radar antenna
			const float CosAngle = FMath::Cos(FMath::DegreesToRadians(ACMBoresightConeAngle));
			return NormLocalDir.X >= CosAngle;
		}

		case ERadarACMSubMode::SlewAcquisition:
		{
			// Conical envelope around TDC slew center
			const FRotator SlewRotator(ScanCenterElevation, ScanCenterAzimuth, 0.0f);
			const FVector SlewDir = SlewRotator.Vector();
			constexpr float SlewConeAngle = 10.0f;
			const float CosCone = FMath::Cos(FMath::DegreesToRadians(SlewConeAngle));
			return FVector::DotProduct(NormLocalDir, SlewDir) >= CosCone;
		}

		default:
			return false;
	}
}

bool UAircraftRadarComponent::ExecuteAuthoritativeSetScanVolume(float InAzimuthWidth, float InElevationHeight, int32 InBars)
{
	AzimuthScanWidth = FMath::Clamp(InAzimuthWidth, 5.0f, 360.0f);
	ElevationScanHeight = FMath::Clamp(InElevationHeight, 2.0f, 120.0f);
	ElevationBars = FMath::Clamp(InBars, 1, 8);
	ScanSizePreset = ERadarScanSize::Custom;

	RecalculateScanFrameTime();

	// Reset scan position
	CurrentScanAzimuth = ScanCenterAzimuth - (AzimuthScanWidth * 0.5f);
	CurrentScanElevation = ElevationBars > 1 ? ScanCenterElevation - ElevationScanHeight * 0.5f : ScanCenterElevation;
	ActiveSampleBeams.Reset();
	bHasPreviousPlateSample = false;
	ElectronicBeamIndex = 0;
	bScanningRight = true;
	CurrentScanBar = 0;
	++DisplayViewRevision;
	if (bEnableTargetCursor && DisplayView.ActiveGeometry == ERadarDisplayGeometry::BScope)
	{
		const float PreviousAzimuth = TDCCursorAzimuth;
		const float DisplayCenter = MakeDisplayProjection(FVector2D::ZeroVector, FVector2D(1.0f, 1.0f)).ScanCenterAzimuth;
		TDCCursorAzimuth = FRotator::NormalizeAxis(DisplayCenter + FMath::Clamp(
			FMath::FindDeltaAngleDegrees(DisplayCenter, TDCCursorAzimuth),
			-AzimuthScanWidth * 0.5f, AzimuthScanWidth * 0.5f));
		if (!FMath::IsNearlyEqual(PreviousAzimuth, TDCCursorAzimuth))
			OnRadarCursorMoved.Broadcast(GetTDCCursorState());
	}
	return true;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeApplyScanSizePreset(ERadarScanSize Preset)
{
	ScanSizePreset = Preset;
	switch (Preset)
	{
		case ERadarScanSize::Narrow_20: AzimuthScanWidth = 20.0f; break;
		case ERadarScanSize::Medium_40: AzimuthScanWidth = 40.0f; break;
		case ERadarScanSize::Wide_60:   AzimuthScanWidth = 60.0f; break;
		case ERadarScanSize::Full_120:  AzimuthScanWidth = 120.0f; break;
		default: break;
	}

	RecalculateScanFrameTime();

	CurrentScanAzimuth = ScanCenterAzimuth - (AzimuthScanWidth * 0.5f);
	CurrentScanElevation = ElevationBars > 1 ? ScanCenterElevation - ElevationScanHeight * 0.5f : ScanCenterElevation;
	ActiveSampleBeams.Reset();
	bHasPreviousPlateSample = false;
	ElectronicBeamIndex = 0;
	bScanningRight = true;
	CurrentScanBar = 0;
	++DisplayViewRevision;
	if (bEnableTargetCursor && DisplayView.ActiveGeometry == ERadarDisplayGeometry::BScope)
	{
		const float PreviousAzimuth = TDCCursorAzimuth;
		const float DisplayCenter = MakeDisplayProjection(FVector2D::ZeroVector, FVector2D(1.0f, 1.0f)).ScanCenterAzimuth;
		TDCCursorAzimuth = FRotator::NormalizeAxis(DisplayCenter + FMath::Clamp(
			FMath::FindDeltaAngleDegrees(DisplayCenter, TDCCursorAzimuth),
			-AzimuthScanWidth * 0.5f, AzimuthScanWidth * 0.5f));
		if (!FMath::IsNearlyEqual(PreviousAzimuth, TDCCursorAzimuth))
			OnRadarCursorMoved.Broadcast(GetTDCCursorState());
	}
	return true;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeOffsetScanCenter(float AzimuthDelta, float ElevationDelta)
{
	ScanCenterAzimuth = FRotator::NormalizeAxis(ScanCenterAzimuth + AzimuthDelta);
	ScanCenterElevation = FMath::Clamp(ScanCenterElevation + ElevationDelta, -89.0f, 89.0f);
	CurrentScanAzimuth += AzimuthDelta;
	CurrentScanElevation += ElevationDelta;
	ActiveSampleBeams.Reset();
	bHasPreviousPlateSample = false;
	++DisplayViewRevision;
	if (bEnableTargetCursor && DisplayView.ActiveGeometry == ERadarDisplayGeometry::BScope)
	{
		const float PreviousAzimuth = TDCCursorAzimuth;
		const float DisplayCenter = MakeDisplayProjection(FVector2D::ZeroVector, FVector2D(1.0f, 1.0f)).ScanCenterAzimuth;
		TDCCursorAzimuth = FRotator::NormalizeAxis(DisplayCenter + FMath::Clamp(
			FMath::FindDeltaAngleDegrees(DisplayCenter, TDCCursorAzimuth),
			-AzimuthScanWidth * 0.5f, AzimuthScanWidth * 0.5f));
		if (!FMath::IsNearlyEqual(PreviousAzimuth, TDCCursorAzimuth))
			OnRadarCursorMoved.Broadcast(GetTDCCursorState());
	}
	return true;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeCycleRangeScale(bool bIncrease)
{
	if (RangeScalePresets.IsEmpty()) return false;
	int32 CurrentIdx = 0;
	float BestDiff = TNumericLimits<float>::Max();
	for (int32 i = 0; i < RangeScalePresets.Num(); ++i)
	{
		const float Diff = FMath::Abs(RangeScalePresets[i] - CurrentDisplayRange);
		if (Diff < BestDiff)
		{
			BestDiff = Diff;
			CurrentIdx = i;
		}
	}
	const int32 Count = RangeScalePresets.Num();
	const int32 NextIdx = bIncrease ? (CurrentIdx + 1) % Count : (CurrentIdx - 1 + Count) % Count;
	return ExecuteAuthoritativeSetDisplayRange(RangeScalePresets[NextIdx]);
}

bool UAircraftRadarComponent::ExecuteAuthoritativeSetDisplayRange(float NewRangeCm)
{
	if (!FMath::IsFinite(NewRangeCm)) return false;
	NewRangeCm = FMath::Clamp(NewRangeCm, 1000.0f, 100000000.0f);
	if (FMath::IsNearlyEqual(CurrentDisplayRange, NewRangeCm))
	{
		return true;
	}

	const float PreviousDisplayRange = CurrentDisplayRange;
	CurrentDisplayRange = NewRangeCm;
	++DisplayViewRevision;
	if (bEnableTargetCursor)
	{
		if (bPreserveCursorDisplayPositionOnRangeChange &&
			FMath::IsFinite(PreviousDisplayRange) && PreviousDisplayRange > 0.0f)
		{
			double MaxCursorRange = CurrentDisplayRange;
			if (DisplayView.ActiveGeometry == ERadarDisplayGeometry::PPI)
			{
				// PPI limits horizontal range. A tilted display reference can require a
				// slant range larger than the scale to retain the same map position.
				FVector DisplayOrigin;
				FRotator ReferenceRotation;
				GetRadarDisplayReferenceTransform(DisplayOrigin, ReferenceRotation);
				const FVector Direction = ReferenceRotation.RotateVector(
					FRotator(TDCCursorElevation, TDCCursorAzimuth, 0.0f).Vector());
				MaxCursorRange /= FMath::Max(Direction.Size2D(), static_cast<double>(SMALL_NUMBER));
			}
			const double ScaledRange = static_cast<double>(TDCCursorRange) * NewRangeCm / PreviousDisplayRange;
			TDCCursorRange = static_cast<float>(FMath::Clamp(ScaledRange, 1.0, MaxCursorRange));
		}
		else TDCCursorRange = FMath::Clamp(TDCCursorRange, 1.0f, CurrentDisplayRange);
	}

	OnRadarDisplayRangeChanged.Broadcast(CurrentDisplayRange);
	if (bEnableTargetCursor) OnRadarCursorMoved.Broadcast(GetTDCCursorState());
	return true;
}

void UAircraftRadarComponent::OnRep_CurrentDisplayRange()
{
	// The coherent operator snapshot emits the display and cursor notifications.
}

FVector2D UAircraftRadarComponent::GetCurrentScanPosition() const
{
	const float HalfAz = AzimuthScanWidth * 0.5f;
	const float NormAz = (HalfAz > 0.0f) ? (CurrentScanAzimuth - ScanCenterAzimuth + HalfAz) / AzimuthScanWidth : 0.5f;
	const float NormEl = (ElevationBars > 1) ? static_cast<float>(CurrentScanBar) / static_cast<float>(ElevationBars - 1) : 0.5f;
	return FVector2D(FMath::Clamp(NormAz, 0.0f, 1.0f), FMath::Clamp(NormEl, 0.0f, 1.0f));
}

bool UAircraftRadarComponent::IsTargetInScanVolume(AActor* Target) const
{
	if (!IsValid(Target) || !IsRadarEmitting())
	{
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	FVector RadarPosition;
	FRotator RadarRotation;
	GetRadarSourceTransform(RadarPosition, RadarRotation);

	const FVector ToTarget = Target->GetActorLocation() - RadarPosition;
	const float DistSq = ToTarget.SizeSquared();

	// 1. Air Combat Maneuver mode: evaluate sub-mode volume (swath/cone) within auto-lock range
	if (RadarMode == ERadarOperatingMode::AirCombatManeuver)
	{
		if (DistSq > FMath::Square(ACMAutoLockRange))
		{
			return false;
		}

		const FVector LocalDir = RadarRotation.UnrotateVector(ToTarget.GetSafeNormal());
		return IsDirectionInACMVolume(LocalDir, ACMSubMode) &&
			(!UsesPhysicalPlateBeam() || MakeBeamSample(0.0f, 0.0f).Contains(Target->GetActorLocation()));
	}

	// 2. Single Target Track: target is in beam if it is the actively illuminated STT lock
	if (RadarMode == ERadarOperatingMode::SingleTargetTrack)
	{
		if (DistSq > FMath::Square(MaxDetectionRange * 1.1f))
		{
			return false;
		}

		if (STTLockedTrackID >= 0)
		{
			const int32 TrackIndex = FindTrackIndex(STTLockedTrackID);
			if (TrackIndex != INDEX_NONE && Tracks[TrackIndex].TrackedActor.Get() == Target)
			{
				float Bearing, Elevation;
				ComputeBearingElevation(Target->GetActorLocation(), Bearing, Elevation);
				return IsWithinAntennaGimbal(Bearing, Elevation, 1.1f) &&
					CalculateAntennaBeamGain(Target->GetActorLocation()) > KINDA_SMALL_NUMBER;
			}
		}
		return false;
	}

	// 3. Spotlight SAR mode: target is in volume if within SpotlightPatchRadius of ground coordinate
	if (RadarMode == ERadarOperatingMode::Spotlight)
	{
		float Bearing, Elevation;
		ComputeBearingElevation(SpotlightTargetLocation, Bearing, Elevation);
		if (!bHasSpotlightPoint || !IsWithinAntennaGimbal(Bearing, Elevation))
		{
			return false;
		}

		const float DistToPatchSq = FVector::DistSquared(Target->GetActorLocation(), SpotlightTargetLocation);
		return DistToPatchSq <= FMath::Square(SpotlightPatchRadius) &&
			(!UsesPhysicalPlateBeam() || MakeBeamSample(0.0f, 0.0f).Contains(Target->GetActorLocation()));
	}

	// 4. Search / TWS / GM / SS modes: evaluate scan volume azimuth and elevation bounds
	if (DistSq > FMath::Square(MaxDetectionRange) || DistSq < FMath::Square(MinDetectionRange)) return false;
	if (UsesPhysicalPlateBeam())
	{
		return MakeBeamSample(0.0f, 0.0f).Contains(Target->GetActorLocation());
	}
	float Bearing, Elevation;
	ComputeBearingElevation(Target->GetActorLocation(), Bearing, Elevation);

	const float HalfAz = AzimuthScanWidth * 0.5f;
	const float HalfEl = ElevationScanHeight * 0.5f;

	const float AzDiff = FMath::Abs(FMath::FindDeltaAngleDegrees(ScanCenterAzimuth, Bearing));
	const float ElDiff = FMath::Abs(Elevation - ScanCenterElevation);

	return (AzimuthScanWidth >= 360.0f || AzDiff <= HalfAz) && ElDiff <= HalfEl;
}

FVector UAircraftRadarComponent::GetCommandedBeamDirection() const
{
	if (RadarMode == ERadarOperatingMode::SingleTargetTrack && IsValid(GetSTTLockedActor()))
		return (GetSTTLockedActor()->GetActorLocation() - GetRadarLocation()).GetSafeNormal();
	if (RadarMode == ERadarOperatingMode::Spotlight && bHasSpotlightPoint)
		return (SpotlightTargetLocation - GetRadarLocation()).GetSafeNormal();
	return GetRadarRotation().RotateVector(FRotator(CurrentScanElevation, CurrentScanAzimuth, 0.0f).Vector());
}

float UAircraftRadarComponent::CalculateDetectionRange(float TargetRCS) const
{
	if (TargetRCS <= 0.0f || MinimumDetectableRCS <= 0.0f)
	{
		return 0.0f;
	}

	// Simplified radar equation: R_det = R_max * (RCS / RCS_min)^(1/4)
	return MaxDetectionRange * FMath::Pow(TargetRCS / MinimumDetectableRCS, 0.25f);
}

float UAircraftRadarComponent::ResolveTargetRCS(const AActor* Candidate, bool& bOutFoundTag, FString& OutParsedTag) const
{
	bOutFoundTag = false;
	OutParsedTag = TEXT("NONE");

	if (!bEnableTargetRCSTagParsing || !IsValid(Candidate))
	{
		return DefaultTargetRCS;
	}

	auto TryParseRCSTag = [](const FName& TagName, float& OutRCS, FString& OutMatchedTag) -> bool
	{
		TCHAR Buffer[FName::StringBufferSize];
		TagName.ToString(Buffer);
		const TCHAR* Str = Buffer;
		if ((FPlatformString::Strnicmp(Str, TEXT("RCS="), 4) == 0) || (FPlatformString::Strnicmp(Str, TEXT("RCS:"), 4) == 0))
		{
			const TCHAR* ValPtr = Str + 4;
			while (*ValPtr == ' ' || *ValPtr == '\t') ++ValPtr;
			const float ParsedVal = FCString::Atof(ValPtr);
			if (ParsedVal > 0.0f)
			{
				OutRCS = ParsedVal;
				OutMatchedTag = Str;
				return true;
			}
		}
		return false;
	};

	// 1. Check Candidate Actor tags
	for (const FName& Tag : Candidate->Tags)
	{
		float FoundRCS = 0.0f;
		FString MatchedStr;
		if (TryParseRCSTag(Tag, FoundRCS, MatchedStr))
		{
			bOutFoundTag = true;
			OutParsedTag = MatchedStr;
			return FoundRCS;
		}
	}

	// 2. Check Candidate Component tags if bSearchComponentTags is enabled
	if (bSearchComponentTags)
	{
		TInlineComponentArray<UActorComponent*> Components(Candidate);
		for (const UActorComponent* Comp : Components)
		{
			if (Comp)
			{
				for (const FName& CompTag : Comp->ComponentTags)
				{
					float FoundRCS = 0.0f;
					FString MatchedStr;
					if (TryParseRCSTag(CompTag, FoundRCS, MatchedStr))
					{
						bOutFoundTag = true;
						OutParsedTag = MatchedStr;
						return FoundRCS;
					}
				}
			}
		}
	}

	// No valid RCS tag found on actor or components — default to unnatural indicator
	return DefaultTargetRCS;
}

float UAircraftRadarComponent::CalculateEffectiveRCS(float BaseRCS, const AActor* TargetActor, const FVector& TargetToRadar) const
{
	if (BaseRCS <= 0.0f)
	{
		return 0.0f;
	}

	if (!bEnableAspectAngleRCS || !IsValid(TargetActor))
	{
		return BaseRCS;
	}

	// Unit line-of-sight vector pointing from target towards radar
	const FVector L_Hat = TargetToRadar.GetSafeNormal();
	if (L_Hat.IsNearlyZero())
	{
		return BaseRCS;
	}

	// Target forward heading vector in world space
	const FVector F_Hat = TargetActor->GetActorForwardVector().GetSafeNormal();

	// Cosine of aspect angle:
	// +1.0 = Nose-on (radar is directly ahead of target)
	//  0.0 = Beam / Broadside (radar is looking at target side)
	// -1.0 = Tail-on (radar is directly behind target)
	const float CosAspect = FMath::Clamp(FVector::DotProduct(F_Hat, L_Hat), -1.0f, 1.0f);

	// sin^2(alpha) = 1 - cos^2(alpha) — peaks at 1.0 on broadside (90 deg / 270 deg)
	const float SinAspectSq = FMath::Clamp(1.0f - (CosAspect * CosAspect), 0.0f, 1.0f);

	// Tail factor: only contributes when viewing rear hemisphere (CosAspect < 0)
	const float TailFactor = FMath::Max(0.0f, -CosAspect);

	// Broadside adds specular surface return from fuselage and vertical stabilizers
	// Tail adds cavity reflections from engine turbine nozzles
	const float BeamFactor = SinAspectSq * FMath::Max(0.0f, RCSAspectBeamMultiplier - 1.0f);
	const float TailAdd = TailFactor * FMath::Max(0.0f, RCSAspectTailMultiplier - 1.0f);

	const float AspectMultiplier = 1.0f + BeamFactor + TailAdd;
	return BaseRCS * AspectMultiplier;
}

bool UAircraftRadarComponent::IsWithinAntennaGimbal(float Bearing, float Elevation, float Margin) const
{
	return bOmnidirectionalTracking ||
		((MaxAntennaGimbalAzimuth >= 180.0f || FMath::Abs(Bearing) <= MaxAntennaGimbalAzimuth * Margin) &&
		 (MaxAntennaGimbalElevation >= 90.0f || FMath::Abs(Elevation) <= MaxAntennaGimbalElevation * Margin));
}

float UAircraftRadarComponent::CalculatePlateGain(const FVector& LocalTargetDir) const
{
	if ((ScanDrive == ERadarScanDrive::PESA || ScanDrive == ERadarScanDrive::AESA) &&
		!(bOmnidirectionalTracking && AzimuthScanWidth >= 360.0f))
	{
		const float CosAngle = FMath::Clamp(LocalTargetDir.X, 0.0f, 1.0f);
		return CosAngle * FMath::Sqrt(CosAngle);
	}
	return 1.0f;
}

float UAircraftRadarComponent::CalculateAntennaBeamGain(const FVector& TargetPosition) const
{
	FVector Origin;
	FRotator PlateRotation;
	GetRadarSourceTransform(Origin, PlateRotation);
	const FVector LocalDirection = PlateRotation.UnrotateVector((TargetPosition - Origin).GetSafeNormal());
	// Tracking/acquisition may steer virtually; animated MSA must receive through the real plate beam.
	if (RadarMode == ERadarOperatingMode::SingleTargetTrack || RadarMode == ERadarOperatingMode::AirCombatManeuver)
	{
		return UsesPhysicalPlateBeam() ? MakeBeamSample(0.0f, 0.0f).GetGain(TargetPosition) : CalculatePlateGain(LocalDirection);
	}
	float Gain = 0.0f;
	for (const FRadarBeamSample& Beam : ActiveSampleBeams)
		Gain = FMath::Max(Gain, Beam.GetGain(TargetPosition));
	if (ActiveSampleBeams.IsEmpty())
		Gain = MakeBeamSample(UsesPhysicalPlateBeam() ? 0.0f : CurrentScanAzimuth,
			UsesPhysicalPlateBeam() ? 0.0f : CurrentScanElevation).GetGain(TargetPosition);
	return Gain * CalculatePlateGain(LocalDirection);
}

float UAircraftRadarComponent::CalculateSignalStrength(float Range, float EffectiveRCS, float BeamGain) const
{
	if (Range <= 0.0f || EffectiveRCS <= 0.0f || MinimumDetectableRCS <= 0.0f)
	{
		return 0.0f;
	}

	// Detection range on boresight for the effective RCS
	const float DetRange = CalculateDetectionRange(EffectiveRCS);
	if (DetRange <= 0.0f)
	{
		return 0.0f;
	}

	// Safe slant range (minimum 1m to prevent divide-by-zero or singularity)
	const float SafeRange = FMath::Max(Range, 100.0f);
	const float ClampedBeamGain = FMath::Clamp(BeamGain, 0.01f, 1.0f);

	// Two-way radar SNR in decibels relative to detection threshold (0 dB at detection limit):
	// SNR_dB = 40 * log10(R_det / R) + 20 * log10(G_beam)
	const float RangeRatio = DetRange / SafeRange;
	if (RangeRatio <= 0.0f)
	{
		return 0.0f;
	}

	const float DynamicRange = FMath::Max(10.0f, ReceiverDynamicRangeDB);
	const float SNR_dB = 40.0f * FMath::LogX(10.0f, RangeRatio) + 20.0f * FMath::LogX(10.0f, ClampedBeamGain);

	// Normalize between 0.0 (detection threshold) and 1.0 (receiver dynamic range saturation)
	return FMath::Clamp(SNR_dB / DynamicRange, 0.0f, 1.0f);
}

bool UAircraftRadarComponent::ExecuteAuthoritativeLockTrack(int32 TrackID)
{
	const int32 TrackIndex = FindTrackIndex(TrackID);
	if (TrackIndex == INDEX_NONE || !Tracks[TrackIndex].TrackedActor.IsValid() ||
		Tracks[TrackIndex].Status == ERadarTrackStatus::Lost)
	{
		return false;
	}
	ClearLinkedDesignation();
	SelectedContactID = Tracks[TrackIndex].ContactID;
	if (RadarMode == ERadarOperatingMode::SingleTargetTrack && STTLockedTrackID == TrackID)
		return true;
	if (STTLockedTrackID >= 0 && STTLockedTrackID != TrackID)
	{
		const int32 OldLockID = STTLockedTrackID;
		const int32 OldIndex = FindTrackIndex(OldLockID);
		const bool bOldBugged = BuggedTrackID == OldLockID;
		if (OldIndex != INDEX_NONE)
		{
			Tracks[OldIndex].Status = bOldBugged ? ERadarTrackStatus::Bugged :
				(PreSTTMode == ERadarOperatingMode::TrackWhileScan ? ERadarTrackStatus::Tracked : ERadarTrackStatus::Search);
			Tracks[OldIndex].bIsBeamTarget = bOldBugged;
			Tracks[OldIndex].bIsBugged = bOldBugged;
		}
		STTLockedTrackID = -1;
		STTLockedActor = nullptr;
		RecordOperatorEvent(ERadarOperatorEventType::LockLost, OldLockID);
		OnRadarLockLost.Broadcast(OldLockID);
		if (!bOldBugged) OnRadarTrackDeselected.Broadcast(OldLockID);
	}

	// Transition to STT mode
	ExecuteAuthoritativeSetRadarMode(ERadarOperatingMode::SingleTargetTrack);
	STTLockedTrackID = TrackID;
	STTLockedActor = Tracks[TrackIndex].TrackedActor.Get();

	// Update track status
	Tracks[TrackIndex].Status = ERadarTrackStatus::Locked;
	Tracks[TrackIndex].bIsBeamTarget = true;

	RecordOperatorEvent(ERadarOperatorEventType::LockAcquired, TrackID, true, &Tracks[TrackIndex]);
	OnRadarLockAcquired.Broadcast(Tracks[TrackIndex]);
	OnRadarTrackSelected.Broadcast(Tracks[TrackIndex]);
	return true;
}

bool UAircraftRadarComponent::AcquireOrLockActor(AActor* TargetActor, bool bForceSTT)
{
	if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority()) return false;
	if (!IsValid(TargetActor) || TargetActor == GetOwner())
	{
		return false;
	}

	const int32 TrackIndex = FindTrackIndexByActor(TargetActor);
	if (TrackIndex != INDEX_NONE)
	{
		if (bForceSTT || RadarMode == ERadarOperatingMode::SingleTargetTrack || RadarMode == ERadarOperatingMode::AirCombatManeuver)
		{
			return ExecuteAuthoritativeLockTrack(Tracks[TrackIndex].TrackID);
		}
		else
		{
			return ExecuteAuthoritativeBugTrack(Tracks[TrackIndex].TrackID);
		}
	}

	// Actor not yet swept by antenna beam — evaluate candidate immediately
	FRadarTrack RawTrack;
	if (EvaluateCandidate(TargetActor, RawTrack))
	{
		const int32 NewTrackID = CreateTrack(TargetActor, RawTrack);
		if (bForceSTT || RadarMode == ERadarOperatingMode::SingleTargetTrack || RadarMode == ERadarOperatingMode::AirCombatManeuver)
		{
			return ExecuteAuthoritativeLockTrack(NewTrackID);
		}
		else
		{
			return ExecuteAuthoritativeBugTrack(NewTrackID);
		}
	}

	return false;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeBreakLock()
{
	STTLockedActor = nullptr;

	if (STTLockedTrackID >= 0)
	{
		const int32 OldLockID = STTLockedTrackID;
		const int32 TrackIndex = FindTrackIndex(STTLockedTrackID);
		const bool bWasBugged = (BuggedTrackID >= 0 && BuggedTrackID == OldLockID);

		if (TrackIndex != INDEX_NONE)
		{
			if (bWasBugged)
			{
				// Revert to bugged state
				Tracks[TrackIndex].Status = ERadarTrackStatus::Bugged;
				Tracks[TrackIndex].bIsBeamTarget = true;
				Tracks[TrackIndex].bIsBugged = true;
			}
			else if (PreSTTMode == ERadarOperatingMode::TrackWhileScan)
			{
				Tracks[TrackIndex].Status = ERadarTrackStatus::Tracked;
				Tracks[TrackIndex].bIsBeamTarget = false;
				Tracks[TrackIndex].bIsBugged = false;
			}
			else
			{
				Tracks[TrackIndex].Status = ERadarTrackStatus::Search;
				Tracks[TrackIndex].bIsBeamTarget = false;
				Tracks[TrackIndex].bIsBugged = false;
			}
		}

		STTLockedTrackID = -1;
		RecordOperatorEvent(ERadarOperatorEventType::LockLost, OldLockID);
		OnRadarLockLost.Broadcast(OldLockID);

		// Broadcast selection update if still bugged, otherwise deselect
		if (!bWasBugged)
		{
			OnRadarTrackDeselected.Broadcast(OldLockID);
		}
		else if (TrackIndex != INDEX_NONE)
		{
			OnRadarTrackSelected.Broadcast(Tracks[TrackIndex]);
		}
	}

	// Return to previous scan mode (default to Search if PreSTTMode was STT)
	if (PreSTTMode == ERadarOperatingMode::SingleTargetTrack)
	{
		PreSTTMode = ERadarOperatingMode::Search;
	}
	ExecuteAuthoritativeSetRadarMode(PreSTTMode);
	return true;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeBugTrack(int32 TrackID)
{
	const int32 TrackIndex = FindTrackIndex(TrackID);
	if (TrackIndex == INDEX_NONE || !Tracks[TrackIndex].TrackedActor.IsValid() ||
		Tracks[TrackIndex].Status == ERadarTrackStatus::Lost)
	{
		return false;
	}
	ClearLinkedDesignation();
	SelectedContactID = Tracks[TrackIndex].ContactID;
	if (BuggedTrackID == TrackID) return true;

	// Clear previous bugged track
	if (BuggedTrackID >= 0)
	{
		const int32 OldBugID = BuggedTrackID;
		const int32 OldBugIdx = FindTrackIndex(BuggedTrackID);
		if (OldBugIdx != INDEX_NONE)
		{
			const bool bStillLocked = OldBugID == STTLockedTrackID;
			Tracks[OldBugIdx].Status = bStillLocked ? ERadarTrackStatus::Locked :
				(RadarMode == ERadarOperatingMode::TrackWhileScan ? ERadarTrackStatus::Tracked : ERadarTrackStatus::Search);
			Tracks[OldBugIdx].bIsBeamTarget = bStillLocked;
			Tracks[OldBugIdx].bIsBugged = false;
		}
		if (OldBugID != STTLockedTrackID) OnRadarTrackDeselected.Broadcast(OldBugID);
	}

	BuggedTrackID = TrackID;
	Tracks[TrackIndex].Status = TrackID == STTLockedTrackID ? ERadarTrackStatus::Locked : ERadarTrackStatus::Bugged;
	Tracks[TrackIndex].bIsBeamTarget = true;
	Tracks[TrackIndex].bIsBugged = true;

	OnRadarTrackSelected.Broadcast(Tracks[TrackIndex]);
	return true;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeClearBugTrack()
{
	if (BuggedTrackID >= 0)
	{
		const int32 OldBugID = BuggedTrackID;
		const int32 TrackIndex = FindTrackIndex(BuggedTrackID);
		if (TrackIndex != INDEX_NONE)
		{
			const bool bStillLocked = OldBugID == STTLockedTrackID;
			Tracks[TrackIndex].Status = bStillLocked ? ERadarTrackStatus::Locked :
				(RadarMode == ERadarOperatingMode::TrackWhileScan ? ERadarTrackStatus::Tracked : ERadarTrackStatus::Search);
			Tracks[TrackIndex].bIsBeamTarget = bStillLocked;
			Tracks[TrackIndex].bIsBugged = false;
		}
		BuggedTrackID = -1;
		if (OldBugID != STTLockedTrackID) OnRadarTrackDeselected.Broadcast(OldBugID);
	}
	return true;
}

bool UAircraftRadarComponent::IsSTTLocked() const
{
	return RadarMode == ERadarOperatingMode::SingleTargetTrack && STTLockedTrackID >= 0;
}

AActor* UAircraftRadarComponent::GetSTTLockedActor() const
{
	if (STTLockedActor)
	{
		return STTLockedActor;
	}

	if (STTLockedTrackID < 0)
	{
		return nullptr;
	}

	const int32 TrackIndex = FindTrackIndex(STTLockedTrackID);
	if (TrackIndex != INDEX_NONE && Tracks[TrackIndex].TrackedActor.IsValid())
	{
		return Tracks[TrackIndex].TrackedActor.Get();
	}

	return nullptr;
}

bool UAircraftRadarComponent::GetSelectedTrack(FRadarTrack& OutTrack) const
{
	if (GetBestTrackForContact(SelectedContactID, OutTrack)) return true;
	if (STTLockedTrackID >= 0)
	{
		const int32 STTIdx = FindTrackIndex(STTLockedTrackID);
		if (STTIdx != INDEX_NONE)
		{
			OutTrack = Tracks[STTIdx];
			return true;
		}
	}

	if (BuggedTrackID >= 0)
	{
		const int32 BugIdx = FindTrackIndex(BuggedTrackID);
		if (BugIdx != INDEX_NONE)
		{
			OutTrack = Tracks[BugIdx];
			return true;
		}
	}
	if (GetLinkedTrackByID(SelectedLinkedTrackID, OutTrack)) return true;

	OutTrack = FRadarTrack();
	return false;
}

AActor* UAircraftRadarComponent::GetSelectedTargetActor() const
{
	FRadarTrack Track;
	if (GetSelectedTrack(Track) && Track.TrackedActor.IsValid())
	{
		return Track.TrackedActor.Get();
	}
	return nullptr;
}

void UAircraftRadarComponent::GetTracksByStatus(ERadarTrackStatus InStatus, TArray<FRadarTrack>& OutTracks) const
{
	OutTracks.Reset();
	for (const FRadarTrack& Track : Tracks)
	{
		if (Track.Status == InStatus)
		{
			OutTracks.Add(Track);
		}
	}
}

bool UAircraftRadarComponent::GetTrackByID(int32 TrackID, FRadarTrack& OutTrack) const
{
	const int32 Index = FindTrackIndex(TrackID);
	if (Index != INDEX_NONE)
	{
		OutTrack = Tracks[Index];
		return true;
	}
	return false;
}

bool UAircraftRadarComponent::GetTrackByActor(const AActor* TargetActor, FRadarTrack& OutTrack) const
{
	const int32 Index = FindTrackIndexByActor(TargetActor);
	if (Index != INDEX_NONE)
	{
		OutTrack = Tracks[Index];
		return true;
	}
	return false;
}

AActor* UAircraftRadarComponent::GetClosestContact() const
{
	AActor* Closest = nullptr;
	float MinRange = TNumericLimits<float>::Max();

	for (const FRadarTrack& Track : Tracks)
	{
		if (Track.TrackedActor.IsValid() && Track.Range < MinRange)
		{
			MinRange = Track.Range;
			Closest = Track.TrackedActor.Get();
		}
	}
	return Closest;
}

float UAircraftRadarComponent::GetBearingToTrack(int32 TrackID) const
{
	const int32 Index = FindTrackIndex(TrackID);
	return (Index != INDEX_NONE) ? Tracks[Index].Bearing : 0.0f;
}

float UAircraftRadarComponent::GetRangeToTrack(int32 TrackID) const
{
	const int32 Index = FindTrackIndex(TrackID);
	return (Index != INDEX_NONE) ? Tracks[Index].Range : 0.0f;
}

float UAircraftRadarComponent::GetClosureRateToTrack(int32 TrackID) const
{
	const int32 Index = FindTrackIndex(TrackID);
	return (Index != INDEX_NONE) ? Tracks[Index].ClosureRate : 0.0f;
}

bool UAircraftRadarComponent::GetHighestThreatTrack(FRadarTrack& OutTrack) const
{
	if (Tracks.Num() == 0)
	{
		return false;
	}

	// Threat priority: Highest closure rate + closest range
	float HighestThreatScore = -TNumericLimits<float>::Max();
	int32 BestIndex = INDEX_NONE;

	for (int32 i = 0; i < Tracks.Num(); ++i)
	{
		const FRadarTrack& Track = Tracks[i];
		if (!Track.TrackedActor.IsValid())
		{
			continue;
		}

		// Threat score: closure rate weighted by inverse range
		const float NormRange = (MaxDetectionRange > 0.0f) ? FMath::Clamp(1.0f - (Track.Range / MaxDetectionRange), 0.0f, 1.0f) : 0.0f;
		const float NormClosure = (Track.ClosureRate > 0.0f) ? FMath::Clamp(Track.ClosureRate / 50000.0f, 0.0f, 1.0f) : 0.0f;
		const float ThreatScore = (NormClosure * 0.6f) + (NormRange * 0.4f);

		if (ThreatScore > HighestThreatScore)
		{
			HighestThreatScore = ThreatScore;
			BestIndex = i;
		}
	}

	if (BestIndex != INDEX_NONE)
	{
		OutTrack = Tracks[BestIndex];
		return true;
	}
	return false;
}

void UAircraftRadarComponent::GatherCandidateActors(const FVector& Origin, float Range, TArray<AActor*>& OutCandidates) const
{
	OutCandidates.Reset();
	if (Range <= 0.0f)
	{
		return;
	}

	const AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	UAircraftCombatSubsystem* Subsystem = UAircraftCombatSubsystem::Get(this);

	TSet<AActor*> UniqueSet;

	// 1. Fast path: Subsystem spatial registry
	if (Subsystem)
	{
		TArray<AActor*> SubsystemActors;
		Subsystem->GetCombatActorsInVolume(Origin, Range, SubsystemActors);
		for (AActor* Actor : SubsystemActors)
		{
			if (Actor && Actor != OwnerActor)
			{
				UniqueSet.Add(Actor);
			}
		}
	}

	// 2. Physics discovery runs less often than beam samples. The expanded radius covers
	// fast movers that enter the sensor volume between discovery passes.
	if (!bEnablePhysicsCandidateDiscovery && !CachedDiscoveryActors.IsEmpty())
	{
		CachedDiscoveryActors.Reset();
	}
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const bool bRefreshDiscovery = bEnablePhysicsCandidateDiscovery && World &&
		(Now - LastCandidateDiscoveryTime >= FMath::Max(0.1f, CandidateDiscoveryInterval) ||
		FVector::DistSquared(Origin, LastCandidateDiscoveryOrigin) > FMath::Square(DiscoveryDisplacementThresholdCm) ||
		!FMath::IsNearlyEqual(Range, LastCandidateDiscoveryRange, DiscoveryRangeDeltaThresholdCm));
	if (bRefreshDiscovery)
	{
		CachedDiscoveryActors.Reset();
		LastCandidateDiscoveryTime = Now;
		LastCandidateDiscoveryOrigin = Origin;
		LastCandidateDiscoveryRange = Range;
		TArray<FOverlapResult> OverlapResults;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RadarGatherCandidates), false, OwnerActor);
		QueryParams.AddIgnoredActor(OwnerActor);
		if (OwnerActor)
		{
			TInlineComponentArray<UPrimitiveComponent*> OwnerPrimitives(OwnerActor);
			for (UPrimitiveComponent* Prim : OwnerPrimitives)
			{
				if (Prim)
				{
					QueryParams.AddIgnoredComponent(Prim);
				}
			}
		}

		const FCollisionShape SphereShape = FCollisionShape::MakeSphere(Range + CandidateDiscoveryExpandedRadiusCm);

		if (bQueryAllDynamicObjects)
		{
			FCollisionObjectQueryParams ObjectParams;
			ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
			ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
			ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
			ObjectParams.AddObjectTypesToQuery(ECC_Vehicle);

			World->OverlapMultiByObjectType(OverlapResults, Origin, FQuat::Identity, ObjectParams, SphereShape, QueryParams);

			if (OverlapResults.IsEmpty())
			{
				World->OverlapMultiByChannel(OverlapResults, Origin, FQuat::Identity, DetectionChannel, SphereShape, QueryParams);
			}
		}
		else
		{
			World->OverlapMultiByChannel(OverlapResults, Origin, FQuat::Identity, DetectionChannel, SphereShape, QueryParams);
		}

		for (const FOverlapResult& Overlap : OverlapResults)
		{
			if (AActor* Candidate = Overlap.GetActor())
			{
				if (Candidate != OwnerActor)
				{
					CachedDiscoveryActors.AddUnique(Candidate);
					// Auto-register candidate so future ticks resolve immediately via subsystem
					if (Subsystem)
					{
						Subsystem->RegisterCombatActor(Candidate);
					}
				}
			}
		}
		const APawn* DebugPawn = Cast<APawn>(OwnerActor);
		if (bEnableDebugTraces && bDebugPhysicsDiscovery &&
			(!bDebugOnlyPlayerControlled || !DebugPawn || DebugPawn->IsLocallyControlled()))
		{
			DrawDebugString(World, Origin + FVector(0, 0, 1500.0f),
				FString::Printf(TEXT("%s DISCOVERY: %d overlap hits, %d registered candidates"),
					*GetNameSafe(OwnerActor), OverlapResults.Num(), CachedDiscoveryActors.Num()),
				nullptr, FColor::Cyan, FMath::Max(0.1f, CandidateDiscoveryInterval), false);
		}
	}
	if (bEnablePhysicsCandidateDiscovery) for (const TWeakObjectPtr<AActor>& Candidate : CachedDiscoveryActors)
	{
		if (AActor* Actor = Candidate.Get())
		{
			if (FVector::DistSquared(Origin, Actor->GetActorLocation()) <= FMath::Square(Range))
				UniqueSet.Add(Actor);
		}
	}

	OutCandidates = UniqueSet.Array();
	const int32 CandidateLimit = FMath::Clamp(MaxCandidatesPerSample, 16, 4096);
	if (OutCandidates.Num() > CandidateLimit)
	{
		TSet<const AActor*> ExistingTrackActors;
		for (const FRadarTrack& Track : Tracks)
		{
			if (const AActor* Actor = Track.TrackedActor.Get()) ExistingTrackActors.Add(Actor);
		}
		OutCandidates.Sort([&ExistingTrackActors, &Origin](const AActor& A, const AActor& B)
		{
			const bool bAExisting = ExistingTrackActors.Contains(&A);
			const bool bBExisting = ExistingTrackActors.Contains(&B);
			if (bAExisting != bBExisting) return bAExisting;
			return FVector::DistSquared(Origin, A.GetActorLocation()) <
				FVector::DistSquared(Origin, B.GetActorLocation());
		});
		OutCandidates.SetNum(CandidateLimit, EAllowShrinking::No);
	}
}

bool UAircraftRadarComponent::ExecuteAuthoritativeMoveTDCCursor(FVector2D DeltaAxis)
{
	if (!bEnableTargetCursor) return false;
	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	if (DeltaTime <= 0.0f || (FMath::IsNearlyZero(DeltaAxis.X) && FMath::IsNearlyZero(DeltaAxis.Y)))
	{
		return false;
	}

	const FRadarDisplayProjection Projection = MakeDisplayProjection(FVector2D::ZeroVector,
		FVector2D(1.0f, 1.0f), DisplayView.ActiveGeometry, DisplayView.bHeadingUp);
	FVector2D Pixel;
	FRadarDisplayGeometryMath::Project(GetTDCCursorWorldLocation(), Projection, Pixel);
	const float HorizontalSpeed = DisplayView.ActiveGeometry == ERadarDisplayGeometry::BScope
		? CursorAzimuthSpeed / FMath::Max(1.0f, AzimuthScanWidth) : CursorDisplaySpeedFraction;
	const float VerticalSpeed = DisplayView.ActiveGeometry == ERadarDisplayGeometry::BScope
		? CursorRangeSpeedFraction : CursorDisplaySpeedFraction;
	const FVector2D NewPixel(
		FMath::Clamp(Pixel.X + FMath::Clamp(DeltaAxis.X, -1.0f, 1.0f) * HorizontalSpeed * DeltaTime, 0.0f, 1.0f),
		FMath::Clamp(Pixel.Y - FMath::Clamp(DeltaAxis.Y, -1.0f, 1.0f) * VerticalSpeed * DeltaTime, 0.0f, 1.0f));
	return SetCursorFromDisplayPoint(NewPixel, FVector2D(1.0f, 1.0f));
}

bool UAircraftRadarComponent::ResolveDisplayPointToCursor(const FVector2D& WidgetPosition,
	const FRadarDisplayProjection& Projection, FRadarCursorState& OutCursor) const
{
	FVector WorldPoint;
	if (!FRadarDisplayGeometryMath::Unproject(WidgetPosition, Projection, WorldPoint)) return false;
	OutCursor.ElevationDegrees = TDCCursorElevation;
	const FVector Delta = WorldPoint - Projection.RadarOrigin;
	if (Projection.Geometry == ERadarDisplayGeometry::BScope)
	{
		const FVector Local = Projection.ReferenceRotation.UnrotateVector(Delta);
		OutCursor.AzimuthDegrees = Local.Rotation().Yaw;
		OutCursor.SlantRangeCm = Local.Size();
		return true;
	}
	// A PPI has no elevation axis. Solve the 3D cursor ray whose horizontal
	// projection lands exactly at the chosen map point, retaining its elevation.
	const FVector Horizontal(Delta.X, Delta.Y, 0.0f);
	if (Horizontal.Size2D() < 1.0f)
	{
		OutCursor.AzimuthDegrees = TDCCursorAzimuth;
		OutCursor.SlantRangeCm = 1.0f;
		return true;
	}
	const float Elevation = FMath::DegreesToRadians(OutCursor.ElevationDegrees);
	const FVector AxisX = Projection.ReferenceRotation.RotateVector(FVector::ForwardVector) * FMath::Cos(Elevation);
	const FVector AxisY = Projection.ReferenceRotation.RotateVector(FVector::RightVector) * FMath::Cos(Elevation);
	const FVector AxisZ = Projection.ReferenceRotation.RotateVector(FVector::UpVector) * FMath::Sin(Elevation);
	const FVector Desired = Horizontal.GetSafeNormal2D();
	const FVector Perpendicular(-Desired.Y, Desired.X, 0.0f);
	const float P = FVector::DotProduct(Perpendicular, AxisX);
	const float Q = FVector::DotProduct(Perpendicular, AxisY);
	const float R = FVector::DotProduct(Perpendicular, AxisZ);
	const float Amplitude = FMath::Sqrt(P * P + Q * Q);
	if (Amplitude < KINDA_SMALL_NUMBER || FMath::Abs(R) > Amplitude + KINDA_SMALL_NUMBER)
		return false;
	const float CenterAngle = FMath::Atan2(Q, P);
	const float AngleDelta = FMath::Acos(FMath::Clamp(-R / Amplitude, -1.0f, 1.0f));
	float BestScale = -1.0f;
	float BestAzimuth = 0.0f;
	for (const float Candidate : {CenterAngle + AngleDelta, CenterAngle - AngleDelta})
	{
		const FVector Direction = AxisX * FMath::Cos(Candidate) +
			AxisY * FMath::Sin(Candidate) + AxisZ;
		const float Along = FVector::DotProduct(Direction, Desired);
		if (Along > BestScale)
		{
			BestScale = Along;
			BestAzimuth = Candidate;
		}
	}
	if (BestScale < 0.01f) return false;
	OutCursor.AzimuthDegrees = FRotator::NormalizeAxis(FMath::RadiansToDegrees(BestAzimuth));
	OutCursor.SlantRangeCm = Horizontal.Size2D() / BestScale;
	return FMath::IsFinite(OutCursor.SlantRangeCm);
}

bool UAircraftRadarComponent::SetCursorFromDisplayPoint(const FVector2D& WidgetPosition,
	const FVector2D& WidgetSize)
{
	if (!bEnableTargetCursor) return false;
	const FRadarDisplayProjection Projection = MakeDisplayProjection(FVector2D::ZeroVector,
		WidgetSize, DisplayView.ActiveGeometry, DisplayView.bHeadingUp);
	FRadarCursorState Cursor;
	if (!ResolveDisplayPointToCursor(WidgetPosition, Projection, Cursor) || !IsCursorWithinLimits(Cursor))
		return false;
	const bool bChanged = !FMath::IsNearlyEqual(TDCCursorAzimuth, Cursor.AzimuthDegrees) ||
		!FMath::IsNearlyEqual(TDCCursorRange, Cursor.SlantRangeCm);
	TDCCursorAzimuth = FRotator::NormalizeAxis(Cursor.AzimuthDegrees);
	TDCCursorRange = Cursor.SlantRangeCm;
	if (bChanged) OnRadarCursorMoved.Broadcast(GetTDCCursorState());
	return true;
}

FRadarCursorState UAircraftRadarComponent::GetTDCCursorState() const
{
	FRadarCursorState Result;
	if (!bEnableTargetCursor) return Result;
	Result.AzimuthDegrees = TDCCursorAzimuth;
	Result.ElevationDegrees = TDCCursorElevation;
	Result.SlantRangeCm = TDCCursorRange;
	Result.WorldLocation = FRadarDisplayGeometryMath::CursorToWorld(Result,
		MakeDisplayProjection(FVector2D::ZeroVector, FVector2D(1.0f, 1.0f)));
	return Result;
}

bool UAircraftRadarComponent::IsCursorWithinLimits(const FRadarCursorState& Cursor) const
{
	if (!bEnableTargetCursor) return false;
	if (!FMath::IsFinite(Cursor.AzimuthDegrees) || !FMath::IsFinite(Cursor.ElevationDegrees) ||
		!FMath::IsFinite(Cursor.SlantRangeCm)) return false;
	const float DisplayCenter = MakeDisplayProjection(FVector2D::ZeroVector, FVector2D(1.0f, 1.0f)).ScanCenterAzimuth;
	const float AzDelta = FMath::FindDeltaAngleDegrees(DisplayCenter, Cursor.AzimuthDegrees);
	FVector ReferenceLocation;
	FRotator ReferenceRotation;
	GetRadarDisplayReferenceTransform(ReferenceLocation, ReferenceRotation);
	const FVector Direction = ReferenceRotation.RotateVector(FRotator(Cursor.ElevationDegrees,
		Cursor.AzimuthDegrees, 0.0f).Vector());
	const float EffectiveRange = DisplayView.ActiveGeometry == ERadarDisplayGeometry::PPI
		? Cursor.SlantRangeCm * Direction.Size2D() : Cursor.SlantRangeCm;
	return (DisplayView.ActiveGeometry == ERadarDisplayGeometry::PPI || AzimuthScanWidth >= 360.0f ||
		FMath::Abs(AzDelta) <= AzimuthScanWidth * 0.5f + KINDA_SMALL_NUMBER) &&
		FMath::Abs(Cursor.ElevationDegrees) <= MaxAntennaGimbalElevation + KINDA_SMALL_NUMBER &&
		Cursor.SlantRangeCm >= 1.0f && EffectiveRange <= CurrentDisplayRange + KINDA_SMALL_NUMBER;
}

FVector UAircraftRadarComponent::GetTDCCursorWorldLocation() const
{
	return GetTDCCursorState().WorldLocation;
}

bool UAircraftRadarComponent::TryGetTDCCursorWorldLocation(FVector& OutWorldLocation) const
{
	OutWorldLocation = GetTDCCursorWorldLocation();
	return bEnableTargetCursor;
}

bool UAircraftRadarComponent::GetTrackDisplayWorldPosition(int32 TrackID, FVector& OutWorldPosition) const
{
	const int32 Index = FindTrackIndex(TrackID);
	const FRadarTrack* Track = Index != INDEX_NONE ? &Tracks[Index] : LinkedTracks.FindByPredicate(
		[TrackID](const FRadarTrack& Candidate) { return Candidate.TrackID == TrackID; });
	if (!Track)
	{
		OutWorldPosition = FVector::ZeroVector;
		return false;
	}
	OutWorldPosition = Track->LastKnownPosition + Track->EstimatedVelocity * FMath::Clamp(Track->TrackAge, 0.0f, 2.0f);
	return true;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeDesignateUnderCursor()
{
	if (!bEnableTargetCursor) return false;
	const FRadarDisplayProjection Projection = MakeDisplayProjection(FVector2D::ZeroVector,
		FVector2D(1.0f, 1.0f), DisplayView.ActiveGeometry, DisplayView.bHeadingUp);
	FVector2D CursorPixel;
	if (!FRadarDisplayGeometryMath::Project(GetTDCCursorWorldLocation(), Projection, CursorPixel))
	{
		RecordOperatorEvent(ERadarOperatorEventType::CursorDesignated, -1, false);
		OnRadarCursorDesignated.Broadcast(false, -1);
		return false;
	}
	const float GateSq = FMath::Square(FMath::Clamp(CursorSelectionRadiusFraction, 0.005f, 0.2f));
	int32 ClosestID = -1;
	float ClosestSq = GateSq;
	TArray<FRadarTrack> DisplayTracks;
	GetDisplayTracks(DisplayTracks);
	for (const FRadarTrack& Track : DisplayTracks)
	{
		if (Track.Status == ERadarTrackStatus::Lost ||
			(Track.Source == ERadarTrackSource::Local && !Track.TrackedActor.IsValid()))
		{
			continue;
		}
		FVector2D TrackPixel;
		if (FRadarDisplayGeometryMath::Project(Track.LastKnownPosition, Projection, TrackPixel))
		{
			const float DistanceSq = FVector2D::DistSquared(CursorPixel, TrackPixel);
			if (DistanceSq <= ClosestSq)
			{
				ClosestSq = DistanceSq;
				ClosestID = Track.TrackID;
			}
		}
	}
	const bool bLinked = ClosestID < -1;
	const bool bResult = ClosestID != -1 && (bLinked ? ExecuteAuthoritativeDesignateLinkedTrack(ClosestID) :
		(BuggedTrackID == ClosestID ? ExecuteAuthoritativeLockTrack(ClosestID) : ExecuteAuthoritativeBugTrack(ClosestID)));
	if (bEnableDebugTraces && bDebugCursor && GetWorld())
	{
		const FVector Origin = GetRadarLocation();
		if (ClosestID != -1)
		{
			if (const FRadarTrack* Selected = DisplayTracks.FindByPredicate(
				[ClosestID](const FRadarTrack& Track) { return Track.TrackID == ClosestID; }))
				DrawDebugLine(GetWorld(), GetTDCCursorWorldLocation(), Selected->LastKnownPosition,
					bResult ? FColor::Green : FColor::Red, false, 1.0f, 0, 2.0f);
		}
		DrawDebugString(GetWorld(), Origin + FVector(0, 0, 500),
			FString::Printf(TEXT("TDC %s: %s track %d (gate %.3f)"),
				DisplayView.ActiveGeometry == ERadarDisplayGeometry::BScope ? TEXT("B-SCOPE") : TEXT("PPI"),
				bResult ? TEXT("SELECTED") : TEXT("NO VALID CONTACT"), ClosestID, FMath::Sqrt(GateSq)),
			nullptr, bResult ? FColor::Green : FColor::Red, 1.0f, false);
	}
	RecordOperatorEvent(ERadarOperatorEventType::CursorDesignated, bResult ? ClosestID : -1, bResult);
	OnRadarCursorDesignated.Broadcast(bResult, bResult ? ClosestID : -1);
	return bResult;
}

void UAircraftRadarComponent::UndesignateTarget()
{
	if (IsSTTLocked())
	{
		ExecuteAuthoritativeBreakLock();
	}
	else if (BuggedTrackID != -1)
	{
		ExecuteAuthoritativeClearBugTrack();
	}
}

bool UAircraftRadarComponent::CycleTargetDesignation(bool bForward)
{
	if (Tracks.Num() == 0)
	{
		return false;
	}

	int32 CurrentIdx = FindTrackIndex(BuggedTrackID);
	if (CurrentIdx == INDEX_NONE)
	{
		// Bug first valid track if none selected
		for (const FRadarTrack& Track : Tracks)
		{
			if (Track.TrackedActor.IsValid())
			{
				return ExecuteAuthoritativeBugTrack(Track.TrackID);
			}
		}
		return false;
	}

	// Step to next track in cycle
	const int32 NumTracks = Tracks.Num();
	for (int32 Step = 1; Step <= NumTracks; ++Step)
	{
		const int32 NextIdx = bForward ? ((CurrentIdx + Step) % NumTracks) : ((CurrentIdx - Step + NumTracks) % NumTracks);
		if (Tracks[NextIdx].TrackedActor.IsValid())
		{
			return ExecuteAuthoritativeBugTrack(Tracks[NextIdx].TrackID);
		}
	}

	return false;
}

float UAircraftRadarComponent::GetTrackAspectAngle(int32 TrackID) const
{
	const int32 Idx = FindTrackIndex(TrackID);
	if (Idx == INDEX_NONE || !Tracks[Idx].TrackedActor.IsValid())
	{
		return 0.0f;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return 0.0f;
	}

	const FRadarTrack& Track = Tracks[Idx];
	const FVector TargetPos = Track.TrackedActor->GetActorLocation();
	const FVector RadarPos = GetRadarLocation();

	// Line of sight vector from radar out to target
	const FVector RadarToTarget = (TargetPos - RadarPos).GetSafeNormal();

	// Target velocity heading vector
	FVector TargetVelDir = Track.EstimatedVelocity.GetSafeNormal();
	if (TargetVelDir.IsNearlyZero())
	{
		TargetVelDir = Track.TrackedActor->GetActorForwardVector();
	}

	// Aspect angle: angle between target velocity and line of sight away from radar
	// 0 deg = pure tail-on (flying directly away from radar)
	// 180 deg = pure head-on (flying directly toward radar)
	const float Dot = FVector::DotProduct(TargetVelDir, RadarToTarget);
	const float CrossZ = FVector::CrossProduct(TargetVelDir, RadarToTarget).Z;

	const float AspectAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(CrossZ, Dot));
	return AspectAngleDeg;
}

float UAircraftRadarComponent::CalculateHeadingFromVelocity(const FVector& InVelocity, float FallbackHeading, float MinSpeedCmPerSec)
{
	const float HorizontalSpeedSq = (InVelocity.X * InVelocity.X) + (InVelocity.Y * InVelocity.Y);
	const float MinSpeedSq = FMath::Square(FMath::Max(MinSpeedCmPerSec, 0.0f));

	if (HorizontalSpeedSq >= MinSpeedSq && HorizontalSpeedSq > KINDA_SMALL_NUMBER)
	{
		const float HeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(InVelocity.Y, InVelocity.X));
		return FRotator::ClampAxis(HeadingDeg);
	}

	return FRotator::ClampAxis(FallbackHeading);
}

bool UAircraftRadarComponent::ExecuteAuthoritativeDesignateSpotlightPoint(const FVector& WorldLocation)
{
	SpotlightTargetLocation = WorldLocation;
	bHasSpotlightPoint = true;
	SpotlightTrackedActor = nullptr;
	SpotlightDwellAccumulator = 0.0f;
	SpotlightDwellProgress = 0.0f;
	return true;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeDesignateSpotlightActor(AActor* TargetActor)
{
	if (!IsValid(TargetActor)) return false;
	SpotlightTrackedActor = TargetActor;
	SpotlightTargetLocation = TargetActor->GetActorLocation();
	bHasSpotlightPoint = true;
	SpotlightDwellAccumulator = 0.0f;
	SpotlightDwellProgress = 0.0f;
	return true;
}

bool UAircraftRadarComponent::ExecuteAuthoritativeClearSpotlightTarget()
{
	SpotlightTargetLocation = FVector::ZeroVector;
	bHasSpotlightPoint = false;
	SpotlightTrackedActor = nullptr;
	SpotlightDwellAccumulator = 0.0f;
	SpotlightDwellProgress = 0.0f;
	return true;
}

bool UAircraftRadarComponent::ResolveAutoGroundIntersect(FVector& OutGroundLocation) const
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return false;
	}

	const FVector RadarLoc = GetRadarLocation();
	const FRotator RadarRot = GetRadarRotation();

	// Forward vector tilted downward by SpotlightDefaultPitchAngle (e.g. -12°)
	const FVector ForwardVector = RadarRot.Vector();
	const FVector RightVector = FRotationMatrix(RadarRot).GetScaledAxis(EAxis::Y);
	const FVector TraceDirection = ForwardVector.RotateAngleAxis(SpotlightDefaultPitchAngle, RightVector).GetSafeNormal();

	const FVector TraceEnd = RadarLoc + (TraceDirection * MaxDetectionRange);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RadarSpotlightGroundTrace), false, OwnerActor);
	TraceParams.AddIgnoredActor(OwnerActor);
	TArray<AActor*> Attached;
	OwnerActor->GetAttachedActors(Attached, true, true);
	TraceParams.AddIgnoredActors(Attached);

	FHitResult HitResult;
	// Trace against visibility / static world geometry
	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		RadarLoc,
		TraceEnd,
		ECC_Visibility,
		TraceParams
	);

	if (bHit && HitResult.bBlockingHit)
	{
		OutGroundLocation = HitResult.ImpactPoint;
		return true;
	}

	// Fallback: If no geometry hit (e.g. blank testing level without terrain), project to SpotlightDefaultSlantRange
	OutGroundLocation = RadarLoc + (TraceDirection * SpotlightDefaultSlantRange);
	return true;
}

void UAircraftRadarComponent::PerformSpotlightTracking(float DeltaTime)
{
	ActiveSampleBeams.Reset();
	bHasPreviousPlateSample = false;
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	// 1. Maintain or resolve ground coordinate
	if (SpotlightTrackedActor.IsValid())
	{
		SpotlightTargetLocation = SpotlightTrackedActor->GetActorLocation();
		bHasSpotlightPoint = true;
	}
	else if (!bHasSpotlightPoint)
	{
		if (bSpotlightAutoGroundIntersect)
		{
			FVector AutoPoint;
			if (ResolveAutoGroundIntersect(AutoPoint))
			{
				SpotlightTargetLocation = AutoPoint;
				bHasSpotlightPoint = true;
			}
		}

		if (!bHasSpotlightPoint)
		{
			return;
		}
	}

	const FVector RadarLocation = GetRadarLocation();
	const FVector ToSpotlight = SpotlightTargetLocation - RadarLocation;
	const float SlantRange = ToSpotlight.Size();

	// 2. Compute local bearing & elevation to ground target
	float TargetBearing = 0.0f;
	float TargetElevation = 0.0f;
	ComputeBearingElevation(SpotlightTargetLocation, TargetBearing, TargetElevation);

	// The plate is the common steering frame in every drive.
	bSpotlightGimbalExceeded = !IsWithinAntennaGimbal(TargetBearing, TargetElevation);
	CurrentScanAzimuth = TargetBearing;
	CurrentScanElevation = TargetElevation;
	CurrentScanBar = 0;
	if (UsesPhysicalPlateBeam() || !bSpotlightGimbalExceeded)
		ActiveSampleBeams.Add(MakeBeamSample(UsesPhysicalPlateBeam() ? 0.0f : TargetBearing,
			UsesPhysicalPlateBeam() ? 0.0f : TargetElevation));
	const bool bBeamAligned = CalculateAntennaBeamGain(SpotlightTargetLocation) > KINDA_SMALL_NUMBER;

	// 4. Line-of-sight terrain masking check
	bool bIsMasked = false;
	if (bEnableTerrainMasking)
	{
		FHitResult MaskHit;
		if (IsTerrainMasked(RadarLocation, SpotlightTargetLocation, nullptr, &MaskHit))
		{
			// Check if obstacle is significantly closer than the target spot (i.e. mountain blocking view)
			if (MaskHit.bBlockingHit && MaskHit.Distance < (SlantRange - 20000.0f))
			{
				bIsMasked = true;
			}
		}
	}

	// 5. Squint Angle & Doppler Gradient Calculation
	const FVector Velocity = OwnerActor->GetVelocity();
	const float HorizontalSpeed = FVector(Velocity.X, Velocity.Y, 0.0f).Size();
	const FVector LOS_Horiz = FVector(ToSpotlight.X, ToSpotlight.Y, 0.0f).GetSafeNormal();
	const FVector Vel_Horiz = FVector(Velocity.X, Velocity.Y, 0.0f).GetSafeNormal();

	float SquintDeg = 0.0f;
	if (HorizontalSpeed > 100.0f && !LOS_Horiz.IsNearlyZero() && !Vel_Horiz.IsNearlyZero())
	{
		const float DotVelLOS = FMath::Clamp(FVector::DotProduct(Vel_Horiz, LOS_Horiz), -1.0f, 1.0f);
		// Angle between aircraft heading and line of sight to ground point
		SquintDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Abs(DotVelLOS)));
	}
	SpotlightSquintAngle = SquintDeg;

	// In radar theory, if squint angle is too small (< SpotlightMinSquintAngle), Doppler gradient across azimuth is zero
	bSpotlightInBlindCone = (SquintDeg < SpotlightMinSquintAngle);

	// 6. SAR Dwell Integration Accumulator
	if (!bSpotlightGimbalExceeded && !bIsMasked && bBeamAligned && !bSpotlightInBlindCone && HorizontalSpeed > 500.0f)
	{
		// Cross-track velocity scales with sin(squint): maximum at 90°, zero at 0°
		const float SinSquint = FMath::Sin(FMath::DegreesToRadians(FMath::Clamp(SquintDeg, 0.0f, 90.0f)));
		// Scale with nominal aircraft speed (250 m/s ≈ 485 knots)
		const float SpeedFactor = FMath::Clamp(HorizontalSpeed / 25000.0f, 0.3f, 2.0f);

		const float DwellRate = SinSquint * SpeedFactor;
		SpotlightDwellAccumulator += DeltaTime * DwellRate;
		SpotlightDwellProgress = FMath::Clamp(SpotlightDwellAccumulator / FMath::Max(0.5f, SpotlightDwellDuration), 0.0f, 1.0f);

		if (SpotlightDwellProgress >= 1.0f)
		{
			++SARImageRevision;
			RecordOperatorEvent(ERadarOperatorEventType::SARReady, -1, true, nullptr, SpotlightTargetLocation);
			OnSARImageReady.Broadcast(SpotlightTargetLocation);
			SpotlightDwellAccumulator = 0.0f;
		}
	}
	else
	{
		// If geometry degraded or masked, decay dwell progress slightly
		SpotlightDwellAccumulator = FMath::Max(0.0f, SpotlightDwellAccumulator - DeltaTime * 0.5f);
		SpotlightDwellProgress = FMath::Clamp(SpotlightDwellAccumulator / FMath::Max(0.5f, SpotlightDwellDuration), 0.0f, 1.0f);
	}

	// 7. Ground Object Detection & GMTI in Patch Footprint
	if (bSpotlightGimbalExceeded || bIsMasked || !bBeamAligned) return;
	TArray<AActor*> PatchCandidates;
	GatherCandidateActors(SpotlightTargetLocation, SpotlightPatchRadius, PatchCandidates);

	// Process each candidate in patch
	TSet<AActor*> ProcessedActors;
	for (AActor* Candidate : PatchCandidates)
	{
		if (!Candidate || Candidate == OwnerActor || ProcessedActors.Contains(Candidate))
		{
			continue;
		}
		ProcessedActors.Add(Candidate);

		if (!CheckCandidateTags(Candidate))
		{
			continue;
		}

		FRadarTrack RawTrack;
		if (EvaluateCandidate(Candidate, RawTrack))
		{
			RawTrack.bIsGroundTarget = true;
			const float TargetGroundSpeed = FVector(Candidate->GetVelocity().X, Candidate->GetVelocity().Y, 0.0f).Size();
			RawTrack.bIsGMTIMoving = (TargetGroundSpeed >= GMTIVelocityThreshold);

			const int32 ExistingIdx = FindTrackIndexByActor(Candidate);
			if (ExistingIdx != INDEX_NONE)
			{
				UpdateTrack(ExistingIdx, RawTrack);
			}
			else
			{
				CreateTrack(Candidate, RawTrack);
			}
		}
	}
}

#if WITH_EDITOR
void UAircraftRadarComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	DisplayRangeBeforeEdit = CurrentDisplayRange;
	Super::PreEditChange(PropertyAboutToChange);
}

void UAircraftRadarComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UAircraftRadarComponent, CurrentDisplayRange) &&
		DisplayRangeBeforeEdit.IsSet())
	{
		const float EditedRange = CurrentDisplayRange;
		CurrentDisplayRange = DisplayRangeBeforeEdit.GetValue();
		ExecuteAuthoritativeSetDisplayRange(EditedRange);
	}
	DisplayRangeBeforeEdit.Reset();
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : PropertyChangedEvent.GetPropertyName();
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAircraftRadarComponent, ScanSizePreset) ||
		MemberPropertyName == GET_MEMBER_NAME_CHECKED(UAircraftRadarComponent, ScanSizePreset))
	{
		ApplyScanSizePreset(ScanSizePreset);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UAircraftRadarComponent, AzimuthScanWidth) ||
			 MemberPropertyName == GET_MEMBER_NAME_CHECKED(UAircraftRadarComponent, AzimuthScanWidth))
	{
		ScanSizePreset = ERadarScanSize::Custom;
	}
	RecalculateScanFrameTime();
}
#endif

void UAircraftRadarComponent::RegisterGuidingMissile(URadarMissileGuidanceComponent* Missile)
{
	if (!Missile)
	{
		return;
	}

	// Clean stale entries
	ActiveGuidingMissiles.RemoveAll([](const TWeakObjectPtr<URadarMissileGuidanceComponent>& M)
	{
		return !M.IsValid();
	});

	ActiveGuidingMissiles.AddUnique(Missile);
}

void UAircraftRadarComponent::UnregisterGuidingMissile(URadarMissileGuidanceComponent* Missile)
{
	if (!Missile)
	{
		return;
	}

	ActiveGuidingMissiles.Remove(Missile);

	ActiveGuidingMissiles.RemoveAll([](const TWeakObjectPtr<URadarMissileGuidanceComponent>& M)
	{
		return !M.IsValid();
	});
}

bool UAircraftRadarComponent::IsContinuousWaveIlluminating(const AActor* TargetActor) const
{
	if (!IsRadarEmitting())
	{
		return false;
	}

	// 1. Single Target Track (STT) mode actively illuminates the tracked target with dedicated continuous beam
	if (IsSTTLocked())
	{
		AActor* LockedActor = GetSTTLockedActor();
		if (IsValid(LockedActor) && (!TargetActor || LockedActor == TargetActor))
		{
			return IsTargetInScanVolume(LockedActor) &&
				(!bEnableTerrainMasking || !IsTerrainMasked(GetRadarLocation(), LockedActor->GetActorLocation(), LockedActor));
		}
	}

	// 2. Manual Continuous Wave engagement (SAM sites, AI commands, mission scripting)
	if (bManualCWIlluminating)
	{
		if (!TargetActor || (ManualCWTargetActor.IsValid() && ManualCWTargetActor.Get() == TargetActor))
		{
			const AActor* IlluminatedActor = TargetActor ? TargetActor : ManualCWTargetActor.Get();
			if (!IsValid(IlluminatedActor)) return !TargetActor;
			const FVector Position = IlluminatedActor->GetActorLocation();
			float Bearing, Elevation;
			ComputeBearingElevation(Position, Bearing, Elevation);
			const FVector LocalDirection = GetRadarRotation().UnrotateVector((Position - GetRadarLocation()).GetSafeNormal());
			return FVector::DistSquared(GetRadarLocation(), Position) <= FMath::Square(MaxDetectionRange) &&
				IsWithinAntennaGimbal(Bearing, Elevation) && CalculatePlateGain(LocalDirection) > KINDA_SMALL_NUMBER &&
				(!UsesPhysicalPlateBeam() || MakeBeamSample(0.0f, 0.0f).Contains(Position)) &&
				(!bEnableTerrainMasking || !IsTerrainMasked(GetRadarLocation(), Position, IlluminatedActor));
		}
	}

	return false;
}

void UAircraftRadarComponent::SetContinuousWaveIllumination(AActor* TargetActor, bool bEnable)
{
	bManualCWIlluminating = bEnable;
	ManualCWTargetActor = TargetActor;
}

TArray<URadarMissileGuidanceComponent*> UAircraftRadarComponent::GetActiveGuidingMissiles() const
{
	TArray<URadarMissileGuidanceComponent*> Result;
	for (const TWeakObjectPtr<URadarMissileGuidanceComponent>& WeakMissile : ActiveGuidingMissiles)
	{
		if (URadarMissileGuidanceComponent* Missile = WeakMissile.Get())
		{
			Result.Add(Missile);
		}
	}
	return Result;
}
