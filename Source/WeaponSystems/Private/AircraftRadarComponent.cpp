// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------
//
// AircraftRadarComponent.cpp — Core radar logic, mode management, replication, and query helpers
//

#include "AircraftRadarComponent.h"
#include "AircraftCombatSubsystem.h"
#include "AircraftCombatDebug.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"

UAircraftRadarComponent::UAircraftRadarComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	SetIsReplicatedByDefault(true);

	// Default range scale presets (in cm): 20nm, 40nm, 80nm, 160nm
	RangeScalePresets.Add(3704000.0f);   // 20 nm
	RangeScalePresets.Add(7408000.0f);   // 40 nm
	RangeScalePresets.Add(14816000.0f);  // 80 nm
	RangeScalePresets.Add(29632000.0f);  // 160 nm

	CurrentDisplayRange = 7408000.0f; // Default 40nm
}

void UAircraftRadarComponent::BeginPlay()
{
	Super::BeginPlay();

	SetComponentTickEnabled(true);

	if (UAircraftCombatSubsystem* Subsystem = UAircraftCombatSubsystem::Get(this))
	{
		Subsystem->RegisterRadar(this);
		if (AActor* OwnerActor = GetOwner())
		{
			Subsystem->RegisterCombatActor(OwnerActor);
		}
	}
}

void UAircraftRadarComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAircraftCombatSubsystem* Subsystem = UAircraftCombatSubsystem::Get(this))
	{
		Subsystem->UnregisterRadar(this);
	}

	Tracks.Empty();
	STTLockedTrackID = -1;
	BuggedTrackID = -1;
	Super::EndPlay(EndPlayReason);
}

void UAircraftRadarComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const bool bIsEmitting = (RadarMode != ERadarOperatingMode::Off && RadarMode != ERadarOperatingMode::Standby);

	if (bIsEmitting)
	{
		// STT mode: Dedicated tracking on single target
		if (RadarMode == ERadarOperatingMode::SingleTargetTrack)
		{
			PerformSTTTracking();
		}
		// ACM mode: Auto-acquisition
		else if (RadarMode == ERadarOperatingMode::AirCombatManeuver)
		{
			PerformACMAcquisition();
		}
		else
		{
			// Normal scan modes: RWS, TWS, GM, SS
			PerformScanSweep(DeltaTime);
		}

		// Update track files (aging, pruning, velocity smoothing)
		TrackUpdateAccumulator += DeltaTime;
		if (TrackUpdateAccumulator >= TrackUpdateInterval)
		{
			UpdateTrackFiles(TrackUpdateAccumulator);
			TrackUpdateAccumulator = 0.0f;
		}

		// Prune stale tracks every tick (lightweight)
		PruneStaleTracks(DeltaTime);
	}

	// Debug rendering & diagnostics — ALWAYS run if bEnableDebugTraces is enabled
	if (bEnableDebugTraces)
	{
		if (bDrawScanVolume && RadarMode != ERadarOperatingMode::SingleTargetTrack && RadarMode != ERadarOperatingMode::AirCombatManeuver)
		{
			DrawDebugScanVolume();
		}

		if (bDrawTrackSymbology)
		{
			DrawDebugTracks();
		}

		if (bEnableDiagnosticHUD)
		{
			const UEnum* ModeEnum = StaticEnum<ERadarOperatingMode>();
			const FString ModeName = ModeEnum ? ModeEnum->GetDisplayNameTextByValue(static_cast<int64>(RadarMode)).ToString() : TEXT("Unknown");

			if (!bIsEmitting)
			{
				FAircraftCombatDebug::PrintRadarTelemetry(0,
					FString::Printf(TEXT("[RADAR] STATUS: %s (EMCON / SILENT)"), *ModeName),
					FColor::Orange);
			}
			else
			{
				const float ElevationStep = (ElevationBars > 1) ? ElevationScanHeight / static_cast<float>(ElevationBars - 1) : 0.0f;
				const float BarElevation = ScanCenterElevation - (ElevationScanHeight * 0.5f) + (ElevationStep * CurrentScanBar);

				FAircraftCombatDebug::PrintRadarTelemetry(0,
					FString::Printf(TEXT("=== [RADAR] Mode: %s | Tracks: %d | STT: %s | Bugged: %s ==="),
						*ModeName, Tracks.Num(),
						IsSTTLocked() ? *FString::Printf(TEXT("Track #%d"), STTLockedTrackID) : TEXT("NONE"),
						BuggedTrackID != -1 ? *FString::Printf(TEXT("Track #%d"), BuggedTrackID) : TEXT("NONE")),
					FColor::Cyan);

				FAircraftCombatDebug::PrintRadarTelemetry(1,
					FString::Printf(TEXT("Antenna: Az %+.1f° (±%.0f°) | Bar %d/%d (El %+.1f°) | %s"),
						CurrentScanAzimuth, AzimuthScanWidth * 0.5f,
						CurrentScanBar + 1, ElevationBars, BarElevation,
						bScanningRight ? TEXT("RIGHT ->") : TEXT("<- LEFT")),
					FColor::Yellow);

				FAircraftCombatDebug::PrintRadarTelemetry(2,
					FString::Printf(TEXT("Range Scale: %.1f km | Max Instrumented: %.1f km"),
						CurrentDisplayRange / 100000.0f, MaxDetectionRange / 100000.0f),
					FColor::White);
			}
		}
	}
}

void UAircraftRadarComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UAircraftRadarComponent, RadarMode, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAircraftRadarComponent, CurrentScanAzimuth, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAircraftRadarComponent, STTLockedTrackID, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAircraftRadarComponent, Tracks, COND_OwnerOnly);
}

void UAircraftRadarComponent::SetRadarMode(ERadarOperatingMode NewMode)
{
	if (RadarMode == NewMode)
	{
		return;
	}

	const ERadarOperatingMode OldMode = RadarMode;

	// If leaving STT, clear the lock
	if (OldMode == ERadarOperatingMode::SingleTargetTrack && NewMode != ERadarOperatingMode::SingleTargetTrack)
	{
		if (STTLockedTrackID >= 0)
		{
			const int32 OldLockID = STTLockedTrackID;
			STTLockedTrackID = -1;
			OnRadarLockLost.Broadcast(OldLockID);
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
		Tracks.Empty();
		STTLockedTrackID = -1;
		BuggedTrackID = -1;
		OnRadarAllContactsCleared.Broadcast();
	}

	RadarMode = NewMode;

	// Keep tick enabled so diagnostics and standby warnings function
	SetComponentTickEnabled(true);

	// Reset scan position when changing modes
	if (NewMode != ERadarOperatingMode::SingleTargetTrack)
	{
		CurrentScanAzimuth = ScanCenterAzimuth - (AzimuthScanWidth * 0.5f);
		bScanningRight = true;
		CurrentScanBar = 0;
	}

	OnRadarModeChanged.Broadcast(RadarMode);
}

bool UAircraftRadarComponent::IsRadarEmitting() const
{
	return RadarMode != ERadarOperatingMode::Off && RadarMode != ERadarOperatingMode::Standby;
}

void UAircraftRadarComponent::SetACMSubMode(ERadarACMSubMode NewSubMode)
{
	ACMSubMode = NewSubMode;
	if (RadarMode != ERadarOperatingMode::AirCombatManeuver)
	{
		SetRadarMode(ERadarOperatingMode::AirCombatManeuver);
	}
}

void UAircraftRadarComponent::SetScanVolume(float Azimuth, float Elevation, int32 Bars)
{
	AzimuthScanWidth = FMath::Clamp(Azimuth, 5.0f, 360.0f);
	ElevationScanHeight = FMath::Clamp(Elevation, 2.0f, 120.0f);
	ElevationBars = FMath::Clamp(Bars, 1, 8);
	ScanSizePreset = ERadarScanSize::Custom;

	// Reset scan position
	CurrentScanAzimuth = ScanCenterAzimuth - (AzimuthScanWidth * 0.5f);
	bScanningRight = true;
	CurrentScanBar = 0;
}

void UAircraftRadarComponent::ApplyScanSizePreset(ERadarScanSize Preset)
{
	ScanSizePreset = Preset;
	switch (Preset)
	{
		case ERadarScanSize::Narrow_20: AzimuthScanWidth = 20.0f; break;
		case ERadarScanSize::Medium_40: AzimuthScanWidth = 40.0f; break;
		case ERadarScanSize::Wide_60: AzimuthScanWidth = 60.0f; break;
		case ERadarScanSize::Full_120: AzimuthScanWidth = 120.0f; break;
		default: break;
	}

	CurrentScanAzimuth = ScanCenterAzimuth - (AzimuthScanWidth * 0.5f);
	bScanningRight = true;
	CurrentScanBar = 0;
}

void UAircraftRadarComponent::OffsetScanCenter(float AzDelta, float ElDelta)
{
	ScanCenterAzimuth = FMath::Clamp(ScanCenterAzimuth + AzDelta, -90.0f, 90.0f);
	ScanCenterElevation = FMath::Clamp(ScanCenterElevation + ElDelta, -60.0f, 60.0f);
}

void UAircraftRadarComponent::CycleRangeScale(bool bIncrease)
{
	if (RangeScalePresets.Num() == 0)
	{
		return;
	}

	int32 CurrentIdx = INDEX_NONE;
	float MinDiff = TNumericLimits<float>::Max();
	for (int32 i = 0; i < RangeScalePresets.Num(); ++i)
	{
		const float Diff = FMath::Abs(RangeScalePresets[i] - CurrentDisplayRange);
		if (Diff < MinDiff)
		{
			MinDiff = Diff;
			CurrentIdx = i;
		}
	}

	if (CurrentIdx == INDEX_NONE)
	{
		CurrentIdx = 0;
	}

	if (bIncrease)
	{
		CurrentIdx = FMath::Min(CurrentIdx + 1, RangeScalePresets.Num() - 1);
	}
	else
	{
		CurrentIdx = FMath::Max(CurrentIdx - 1, 0);
	}

	CurrentDisplayRange = RangeScalePresets[CurrentIdx];
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

	float Bearing, Elevation;
	ComputeBearingElevation(Target->GetActorLocation(), Bearing, Elevation);

	const float HalfAz = AzimuthScanWidth * 0.5f;
	const float HalfEl = ElevationScanHeight * 0.5f;

	const float AzDiff = FMath::Abs(FMath::FindDeltaAngleDegrees(ScanCenterAzimuth, Bearing));
	const float ElDiff = FMath::Abs(Elevation - ScanCenterElevation);

	return (AzDiff <= HalfAz && ElDiff <= HalfEl);
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

bool UAircraftRadarComponent::CommandLock(int32 TrackID)
{
	const int32 TrackIndex = FindTrackIndex(TrackID);
	if (TrackIndex == INDEX_NONE)
	{
		return false;
	}

	// Transition to STT mode
	SetRadarMode(ERadarOperatingMode::SingleTargetTrack);
	STTLockedTrackID = TrackID;

	// Update track status
	Tracks[TrackIndex].Status = ERadarTrackStatus::Locked;
	Tracks[TrackIndex].bIsBeamTarget = true;

	OnRadarLockAcquired.Broadcast(Tracks[TrackIndex]);
	OnRadarTrackSelected.Broadcast(Tracks[TrackIndex]);
	return true;
}

bool UAircraftRadarComponent::CommandLockActor(AActor* TargetActor)
{
	return AcquireOrLockActor(TargetActor, true);
}

bool UAircraftRadarComponent::AcquireOrLockActor(AActor* TargetActor, bool bForceSTT)
{
	if (!IsValid(TargetActor) || TargetActor == GetOwner())
	{
		return false;
	}

	const int32 TrackIndex = FindTrackIndexByActor(TargetActor);
	if (TrackIndex != INDEX_NONE)
	{
		if (bForceSTT || RadarMode == ERadarOperatingMode::SingleTargetTrack || RadarMode == ERadarOperatingMode::AirCombatManeuver)
		{
			return CommandLock(Tracks[TrackIndex].TrackID);
		}
		else
		{
			return CommandBugTrack(Tracks[TrackIndex].TrackID);
		}
	}

	// Actor not yet swept by antenna beam — evaluate candidate immediately
	FRadarTrack RawTrack;
	if (EvaluateCandidate(TargetActor, RawTrack))
	{
		const int32 NewTrackID = CreateTrack(TargetActor, RawTrack);
		if (bForceSTT || RadarMode == ERadarOperatingMode::SingleTargetTrack || RadarMode == ERadarOperatingMode::AirCombatManeuver)
		{
			return CommandLock(NewTrackID);
		}
		else
		{
			return CommandBugTrack(NewTrackID);
		}
	}

	return false;
}

void UAircraftRadarComponent::BreakLock()
{
	if (STTLockedTrackID >= 0)
	{
		const int32 OldLockID = STTLockedTrackID;
		const int32 TrackIndex = FindTrackIndex(STTLockedTrackID);
		if (TrackIndex != INDEX_NONE)
		{
			Tracks[TrackIndex].Status = ERadarTrackStatus::Tracked;
			Tracks[TrackIndex].bIsBeamTarget = false;
		}

		STTLockedTrackID = -1;
		OnRadarLockLost.Broadcast(OldLockID);
		OnRadarTrackDeselected.Broadcast(OldLockID);
	}

	// Return to previous scan mode (default to Search if PreSTTMode was STT)
	if (PreSTTMode == ERadarOperatingMode::SingleTargetTrack)
	{
		PreSTTMode = ERadarOperatingMode::Search;
	}
	SetRadarMode(PreSTTMode);
}

bool UAircraftRadarComponent::CommandBugTrack(int32 TrackID)
{
	const int32 TrackIndex = FindTrackIndex(TrackID);
	if (TrackIndex == INDEX_NONE)
	{
		return false;
	}

	// Clear previous bug
	if (BuggedTrackID >= 0)
	{
		const int32 OldBugIdx = FindTrackIndex(BuggedTrackID);
		if (OldBugIdx != INDEX_NONE)
		{
			Tracks[OldBugIdx].Status = ERadarTrackStatus::Tracked;
			Tracks[OldBugIdx].bIsBeamTarget = false;
			Tracks[OldBugIdx].bIsBugged = false;
		}
	}

	BuggedTrackID = TrackID;
	Tracks[TrackIndex].Status = ERadarTrackStatus::Bugged;
	Tracks[TrackIndex].bIsBeamTarget = true;
	Tracks[TrackIndex].bIsBugged = true;

	OnRadarTrackSelected.Broadcast(Tracks[TrackIndex]);
	return true;
}

void UAircraftRadarComponent::ClearBugTrack()
{
	if (BuggedTrackID >= 0)
	{
		const int32 OldBugID = BuggedTrackID;
		const int32 TrackIndex = FindTrackIndex(BuggedTrackID);
		if (TrackIndex != INDEX_NONE)
		{
			Tracks[TrackIndex].Status = ERadarTrackStatus::Tracked;
			Tracks[TrackIndex].bIsBeamTarget = false;
			Tracks[TrackIndex].bIsBugged = false;
		}
		BuggedTrackID = -1;
		OnRadarTrackDeselected.Broadcast(OldBugID);
	}
}

bool UAircraftRadarComponent::IsSTTLocked() const
{
	return RadarMode == ERadarOperatingMode::SingleTargetTrack && STTLockedTrackID >= 0;
}

AActor* UAircraftRadarComponent::GetSTTLockedActor() const
{
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

int32 UAircraftRadarComponent::FindTrackIndex(int32 TrackID) const
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

int32 UAircraftRadarComponent::FindTrackIndexByActor(const AActor* InActor) const
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

void UAircraftRadarComponent::SlewTDCCursor(float AzimuthInput, float RangeInput, float DeltaTime, float SlewSpeedDegPerSec, float SlewSpeedFractionPerSec)
{
	const float HalfAz = AzimuthScanWidth * 0.5f;
	const float MinAz = ScanCenterAzimuth - HalfAz;
	const float MaxAz = ScanCenterAzimuth + HalfAz;

	// Slew azimuth
	TDCCursorAzimuth += AzimuthInput * SlewSpeedDegPerSec * DeltaTime;
	TDCCursorAzimuth = FMath::Clamp(TDCCursorAzimuth, MinAz, MaxAz);

	// Slew range proportionally to CurrentDisplayRange
	const float RangeDelta = RangeInput * (CurrentDisplayRange * SlewSpeedFractionPerSec) * DeltaTime;
	TDCCursorRange += RangeDelta;
	TDCCursorRange = FMath::Clamp(TDCCursorRange, MinDetectionRange, CurrentDisplayRange);
}

FVector2D UAircraftRadarComponent::GetTDCCursorScreenPosition() const
{
	const float HalfAz = AzimuthScanWidth * 0.5f;
	const float NormX = (HalfAz > 0.0f) ? (TDCCursorAzimuth - (ScanCenterAzimuth - HalfAz)) / AzimuthScanWidth : 0.5f;
	const float NormY = (CurrentDisplayRange > 0.0f) ? (TDCCursorRange / CurrentDisplayRange) : 0.0f;
	return FVector2D(FMath::Clamp(NormX, 0.0f, 1.0f), FMath::Clamp(NormY, 0.0f, 1.0f));
}

bool UAircraftRadarComponent::DesignateTrackUnderCursor(float AzimuthGateDegrees, float RangeGatePercent)
{
	if (Tracks.Num() == 0)
	{
		return false;
	}

	const float RangeGateCm = CurrentDisplayRange * RangeGatePercent;
	int32 ClosestTrackID = -1;
	float ClosestDistSq = TNumericLimits<float>::Max();

	for (const FRadarTrack& Track : Tracks)
	{
		if (!Track.TrackedActor.IsValid())
		{
			continue;
		}

		const float AzDiff = FMath::Abs(Track.Bearing - TDCCursorAzimuth);
		const float RangeDiff = FMath::Abs(Track.Range - TDCCursorRange);

		if (AzDiff <= AzimuthGateDegrees && RangeDiff <= RangeGateCm)
		{
			const float DistSq = (AzDiff * AzDiff) + FMath::Square(RangeDiff / CurrentDisplayRange * 100.0f);
			if (DistSq < ClosestDistSq)
			{
				ClosestDistSq = DistSq;
				ClosestTrackID = Track.TrackID;
			}
		}
	}

	if (ClosestTrackID != -1)
	{
		// If already bugged, promote to STT lock!
		if (BuggedTrackID == ClosestTrackID)
		{
			return CommandLock(ClosestTrackID);
		}
		else
		{
			// Bug this track
			return CommandBugTrack(ClosestTrackID);
		}
	}

	return false;
}

void UAircraftRadarComponent::HOTAS_Undesignate()
{
	if (IsSTTLocked())
	{
		BreakLock();
	}
	else if (BuggedTrackID != -1)
	{
		ClearBugTrack();
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
		// No track currently bugged -> bug the first valid track
		for (const FRadarTrack& Track : Tracks)
		{
			if (Track.TrackedActor.IsValid())
			{
				return CommandBugTrack(Track.TrackID);
			}
		}
		return false;
	}

	// Step to next track
	const int32 NumTracks = Tracks.Num();
	for (int32 Step = 1; Step <= NumTracks; ++Step)
	{
		const int32 NextIdx = bForward ? ((CurrentIdx + Step) % NumTracks) : ((CurrentIdx - Step + NumTracks) % NumTracks);
		if (Tracks[NextIdx].TrackedActor.IsValid())
		{
			return CommandBugTrack(Tracks[NextIdx].TrackID);
		}
	}

	return false;
}

bool UAircraftRadarComponent::GetTrackBScopePosition(int32 TrackID, FVector2D& OutScreenPos) const
{
	const int32 Idx = FindTrackIndex(TrackID);
	if (Idx == INDEX_NONE)
	{
		OutScreenPos = FVector2D::ZeroVector;
		return false;
	}

	const FRadarTrack& Track = Tracks[Idx];
	const float HalfAz = AzimuthScanWidth * 0.5f;

	// X: -1.0 (Left edge) to +1.0 (Right edge)
	const float AzDelta = FMath::FindDeltaAngleDegrees(ScanCenterAzimuth, Track.Bearing);
	const float NormX = (HalfAz > 0.0f) ? (AzDelta / HalfAz) : 0.0f;

	// Y: 0.0 (Ownship nose / bottom) to 1.0 (Display top / Max range scale)
	const float NormY = (CurrentDisplayRange > 0.0f) ? (Track.Range / CurrentDisplayRange) : 0.0f;

	OutScreenPos = FVector2D(FMath::Clamp(NormX, -1.0f, 1.0f), FMath::Clamp(NormY, 0.0f, 1.0f));
	return true;
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
	const FVector RadarPos = OwnerActor->GetActorLocation();

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

#if WITH_EDITOR
void UAircraftRadarComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAircraftRadarComponent, ScanSizePreset))
	{
		ApplyScanSizePreset(ScanSizePreset);
	}
}
#endif
