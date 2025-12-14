// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

//
// AircraftRadarComponent_Tracking.cpp — Track file management, STT/TWS lock logic, ACM acquisition
//

#include "AircraftRadarComponent.h"
#include "AircraftCombatSubsystem.h"
#include "AircraftCombatDebug.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"

int32 UAircraftRadarComponent::CreateTrack(AActor* DetectedActor, const FRadarTrack& RawDetection)
{
	// Prevent duplicate track creation for the same actor
	if (IsValid(DetectedActor))
	{
		const int32 ExistingIdx = FindTrackIndexByActor(DetectedActor);
		if (ExistingIdx != INDEX_NONE)
		{
			UpdateTrack(ExistingIdx, RawDetection);
			return Tracks[ExistingIdx].TrackID;
		}
	}

	FRadarTrack NewTrack = RawDetection;
	NewTrack.TrackID = NextTrackID++;
	NewTrack.ContactID = AssignContactID(DetectedActor);
	NewTrack.TrackedActor = DetectedActor;
	NewTrack.TrackAge = 0.0f;
	NewTrack.EstimatedVelocity = FVector::ZeroVector; // Estimated from successive returns, never actor truth.

	// In TWS mode, new contacts start as Tracked
	if (RadarMode == ERadarOperatingMode::TrackWhileScan)
	{
		NewTrack.Status = ERadarTrackStatus::Tracked;
	}
	else
	{
		NewTrack.Status = ERadarTrackStatus::Search;
	}

	if (Tracks.Num() >= FMath::Max(1, MaxTrackFiles))
	{
		return INDEX_NONE;
	}
	Tracks.Add(NewTrack);
	OnRadarContactNew.Broadcast(NewTrack);

	return NewTrack.TrackID;
}

void UAircraftRadarComponent::UpdateTrack(int32 TrackIndex, const FRadarTrack& NewDetection)
{
	if (!Tracks.IsValidIndex(TrackIndex))
	{
		return;
	}

	FRadarTrack& Track = Tracks[TrackIndex];
	const FVector PreviousPosition = Track.LastKnownPosition;
	const float TimeSinceReturn = Track.TrackAge;
	const bool bWasNotching = Track.bIsNotching;

	// Preserve track ID, status, and beam target flags
	const int32 SavedTrackID = Track.TrackID;
	const ERadarTrackStatus SavedStatus = Track.Status;
	const bool bSavedBeamTarget = Track.bIsBeamTarget;

	// Update measurement data
	Track.LastKnownPosition = NewDetection.LastKnownPosition;
	if (TimeSinceReturn >= 0.05f)
	{
		const FVector MeasuredVelocity = (NewDetection.LastKnownPosition - PreviousPosition) / TimeSinceReturn;
		Track.EstimatedVelocity = SmoothVelocity(Track.EstimatedVelocity, MeasuredVelocity, 0.35f);
	}
	Track.ClosureRate = NewDetection.ClosureRate;
	Track.Bearing = NewDetection.Bearing;
	Track.Elevation = NewDetection.Elevation;
	Track.Range = NewDetection.Range;
	Track.SignalStrength = NewDetection.SignalStrength;
	Track.AltitudeASL = NewDetection.AltitudeASL;
	Track.TargetHeading = CalculateHeadingFromVelocity(Track.EstimatedVelocity, Track.TargetHeading > 0.0f ? Track.TargetHeading : NewDetection.TargetHeading);
	Track.RCS = NewDetection.RCS;
	Track.EffectiveRCS = NewDetection.EffectiveRCS;
	Track.bHasRCSTag = NewDetection.bHasRCSTag;
	Track.bIsNotching = NewDetection.bIsNotching;
	Track.bIsJamming = NewDetection.bIsJamming;
	Track.TargetDomain = NewDetection.TargetDomain;
	Track.bIsGroundTarget = NewDetection.bIsGroundTarget;
	Track.bIsGMTIMoving = NewDetection.bIsGMTIMoving;

	// Reset age (fresh return)
	Track.TrackAge = 0.0f;

	// Restore preserved fields
	Track.TrackID = SavedTrackID;
	Track.Status = SavedStatus == ERadarTrackStatus::Lost
		? (RadarMode == ERadarOperatingMode::TrackWhileScan ? ERadarTrackStatus::Tracked : ERadarTrackStatus::Search)
		: SavedStatus;
	Track.bIsBeamTarget = bSavedBeamTarget;
	Track.bIsBugged = (SavedTrackID == BuggedTrackID);

	// IFF result already computed on the candidate return
	Track.IFFResult = NewDetection.IFFResult;

	// If this track was a Search hit and we're in TWS, promote to Tracked
	if (Track.Status == ERadarTrackStatus::Search && RadarMode == ERadarOperatingMode::TrackWhileScan)
	{
		Track.Status = ERadarTrackStatus::Tracked;
	}

	// Broadcast notching warning
	if (Track.bIsNotching && !bWasNotching)
	{
		OnRadarTargetNotching.Broadcast(Track);
	}

	OnRadarContactUpdated.Broadcast(Track);
}

void UAircraftRadarComponent::UpdateTrackFiles(float DeltaTime)
{
	(void)DeltaTime;
	for (int32 i = Tracks.Num() - 1; i >= 0; --i)
	{
		if (Tracks[i].TrackedActor.IsValid()) continue;
		const int32 LostTrackID = Tracks[i].TrackID;
		const bool bWasLocked = LostTrackID == STTLockedTrackID;
		const bool bWasBugged = LostTrackID == BuggedTrackID;
		if (bWasLocked)
		{
			STTLockedTrackID = -1;
			STTLockedActor = nullptr;
			RecordOperatorEvent(ERadarOperatorEventType::LockLost, LostTrackID);
			OnRadarLockLost.Broadcast(LostTrackID);
		}
		if (bWasBugged) BuggedTrackID = -1;
		if (bWasLocked || bWasBugged) OnRadarTrackDeselected.Broadcast(LostTrackID);
		Tracks.RemoveAt(i);
		OnRadarContactLost.Broadcast(LostTrackID);
		if (bWasLocked) ExecuteAuthoritativeBreakLock();
	}
}

void UAircraftRadarComponent::PruneStaleTracks(float DeltaTime)
{
	// Scale timeout to at least 1.5x full frame sweep time so tracks never drop halfway through a multi-bar scan
	const float EffectiveDropTimeout = FMath::Max(TrackDropTimeout, CachedScanFrameTime * 1.5f);

	for (int32 i = Tracks.Num() - 1; i >= 0; --i)
	{
		FRadarTrack& Track = Tracks[i];
		Track.TrackAge += DeltaTime;

		// STT locked tracks have a much higher timeout tolerance
		if (Track.TrackID == STTLockedTrackID)
		{
			continue;
		}

		// Check for timeout
		if (Track.TrackAge > EffectiveDropTimeout)
		{
			const int32 LostTrackID = Track.TrackID;

			if (Track.TrackID == BuggedTrackID)
			{
				BuggedTrackID = -1;
				OnRadarTrackDeselected.Broadcast(LostTrackID);
			}

			Tracks.RemoveAt(i);
			OnRadarContactLost.Broadcast(LostTrackID);
		}
		else if (Track.TrackAge > EffectiveDropTimeout * 0.7f)
		{
			// Mark as fading/lost but don't remove yet
			if (Track.Status != ERadarTrackStatus::Locked && Track.Status != ERadarTrackStatus::Bugged)
			{
				Track.Status = ERadarTrackStatus::Lost;
			}
		}
	}
}

void UAircraftRadarComponent::PerformSTTTracking()
{
	ActiveSampleBeams.Reset();
	bHasPreviousPlateSample = false;
	if (STTLockedTrackID < 0)
	{
		// No target locked — revert to scan
		BreakLock();
		return;
	}

	const int32 TrackIndex = FindTrackIndex(STTLockedTrackID);
	if (TrackIndex == INDEX_NONE)
	{
		// Track file lost
		BreakLock();
		return;
	}

	FRadarTrack& Track = Tracks[TrackIndex];

	// Verify target still valid
	if (!Track.TrackedActor.IsValid())
	{
		const int32 OldLockID = STTLockedTrackID;
		if (BuggedTrackID == OldLockID)
		{
			BuggedTrackID = -1;
		}
		STTLockedTrackID = -1;
		STTLockedActor = nullptr;
		Tracks.RemoveAt(TrackIndex);
		RecordOperatorEvent(ERadarOperatorEventType::LockLost, OldLockID);
		OnRadarLockLost.Broadcast(OldLockID);
		OnRadarTrackDeselected.Broadcast(OldLockID);
		OnRadarContactLost.Broadcast(OldLockID);
		BreakLock();
		return;
	}

	// Check antenna gimbal limits (cannot track outside antenna gimbal bounds)
	const FVector TargetPosition = Track.TrackedActor->GetActorLocation();
	float TargetBearing = 0.0f;
	float TargetElevation = 0.0f;
	ComputeBearingElevation(TargetPosition, TargetBearing, TargetElevation);

	// Update antenna pointing azimuth to track locked target (for HUD/MFD B-scope rendering)
	CurrentScanAzimuth = TargetBearing;
	CurrentScanElevation = TargetElevation;
	CurrentScanBar = 0;
	if (UsesPhysicalPlateBeam()) ActiveSampleBeams.Add(MakeBeamSample(0.0f, 0.0f));

	// STT tracking uses physical antenna gimbal limits (e.g. ±60°) unless omnidirectional/ground-turret tracking is enabled
	if (!IsWithinAntennaGimbal(TargetBearing, TargetElevation, 1.1f))
	{
		Track.Status = ERadarTrackStatus::Lost;
		Track.bIsBeamTarget = false;
		BreakLock();
		return;
	}
	if (!UsesPhysicalPlateBeam()) ActiveSampleBeams.Add(MakeBeamSample(TargetBearing, TargetElevation));

	// STT provides continuous high-rate updates — re-evaluate target
	FRadarTrack RawTrack;
	if (EvaluateCandidate(Track.TrackedActor.Get(), RawTrack))
	{
		UpdateTrack(TrackIndex, RawTrack);
		Track.Status = ERadarTrackStatus::Locked;
		Track.bIsBeamTarget = true;
		Track.TrackAge = 0.0f;

		// Check range — if target exits max range, break lock
		if (Track.Range > MaxDetectionRange * 1.1f)
		{
			Track.Status = ERadarTrackStatus::Lost;
			Track.bIsBeamTarget = false;
			BreakLock();
			return;
		}

	}
	else
	{
		if (RawTrack.bIsNotching && !Track.bIsNotching)
		{
			Track.bIsNotching = true;
			OnRadarTargetNotching.Broadcast(Track);
		}
		// Lost detection — target may have gone to ground or out of range
		if (Track.TrackAge > TrackDropTimeout * 0.5f)
		{
			Track.Status = ERadarTrackStatus::Lost;
			Track.bIsBeamTarget = false;
			BreakLock();
		}
	}
}

void UAircraftRadarComponent::PerformACMAcquisition()
{
	UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	FVector RadarPosition;
	FRotator RadarRotation;
	GetRadarSourceTransform(RadarPosition, RadarRotation);
	ActiveSampleBeams.Reset();
	bHasPreviousPlateSample = false;
	CurrentScanBar = 0;

	// In ACM mode, antenna azimuth reflects the acquisition boresight, slew offset, or helmet cue
	if (ACMSubMode == ERadarACMSubMode::SlewAcquisition)
	{
		CurrentScanAzimuth = ScanCenterAzimuth;
		CurrentScanElevation = ScanCenterElevation;
	}
	else if (ACMSubMode == ERadarACMSubMode::HelmetCue && !HelmetLookDirection.IsNearlyZero())
	{
		const FVector LocalHelmet = RadarRotation.UnrotateVector(HelmetLookDirection);
		CurrentScanAzimuth = LocalHelmet.Rotation().Yaw;
		CurrentScanElevation = LocalHelmet.Rotation().Pitch;
		if (LocalHelmet.X <= 0.0f) CurrentScanAzimuth = CurrentScanElevation = 0.0f;
	}
	else
	{
		CurrentScanAzimuth = 0.0f;
		CurrentScanElevation = 0.0f;
	}
	ActiveSampleBeams.Add(MakeBeamSample(UsesPhysicalPlateBeam() ? 0.0f : CurrentScanAzimuth,
		UsesPhysicalPlateBeam() ? 0.0f : CurrentScanElevation));

	// Query potential targets via spatial registry or physics overlap
	TArray<AActor*> CandidateActors;
	GatherCandidateActors(RadarPosition, ACMAutoLockRange, CandidateActors);

	if (CandidateActors.IsEmpty())
	{
		return;
	}

	// Find closest target within ACM cone
	AActor* BestTarget = nullptr;
	FRadarTrack BestRawTrack;
	float BestRangeSq = TNumericLimits<float>::Max();

	for (AActor* Candidate : CandidateActors)
	{
		if (!Candidate)
		{
			continue;
		}

		// Tag filtering (checks actor and component tags)
		if (!CheckCandidateTags(Candidate))
		{
			continue;
		}

		// Domain filtering for ACM mode
		const ERadarTargetDomain CandidateDomain = ResolveCandidateDomain(Candidate);
		if (!IsDomainAllowedForMode(CandidateDomain, RadarMode))
		{
			continue;
		}

		const FVector ToTarget = Candidate->GetActorLocation() - RadarPosition;
		const float RangeSq = ToTarget.SizeSquared();
		if (RangeSq > FMath::Square(ACMAutoLockRange))
		{
			continue;
		}

		const FVector ToTargetDir = ToTarget.GetSafeNormal();
		const FVector LocalTargetDir = RadarRotation.UnrotateVector(ToTargetDir);

		if (IsDirectionInACMVolume(LocalTargetDir, ACMSubMode) && RangeSq < BestRangeSq)
		{
			// Terrain masking check
			if (bEnableTerrainMasking && IsTerrainMasked(RadarPosition, Candidate->GetActorLocation(), Candidate))
			{
				continue;
			}

			FRadarTrack RawTrack;
			if (EvaluateCandidate(Candidate, RawTrack))
			{
				BestRangeSq = RangeSq;
				BestTarget = Candidate;
				BestRawTrack = RawTrack;
			}
		}
	}

	if (BestTarget)
	{
		// Auto-lock the first target found in ACM
		const int32 NewTrackID = CreateTrack(BestTarget, BestRawTrack);
		if (NewTrackID != INDEX_NONE) ExecuteAuthoritativeLockTrack(NewTrackID);
	}
}

FVector UAircraftRadarComponent::SmoothVelocity(const FVector& OldVelocity, const FVector& NewVelocity, float Alpha) const
{
	return FMath::Lerp(OldVelocity, NewVelocity, FMath::Clamp(Alpha, 0.0f, 1.0f));
}

void UAircraftRadarComponent::DrawDebugTracks() const
{
	UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	const FVector RadarPosition = GetRadarLocation();

	// Deduplicate by target actor so each actor has exactly ONE dominant track displayed
	// Priority hierarchy: Locked (5) > Bugged (4) > Tracked (3) > Jammed (2) > Search (1) > Lost (0)
	auto GetTrackPriority = [](ERadarTrackStatus Status) -> int32
	{
		switch (Status)
		{
			case ERadarTrackStatus::Locked:  return 5;
			case ERadarTrackStatus::Bugged:  return 4;
			case ERadarTrackStatus::Tracked: return 3;
			case ERadarTrackStatus::Jammed:  return 2;
			case ERadarTrackStatus::Search:  return 1;
			case ERadarTrackStatus::Lost:
			default:                         return 0;
		}
	};

	TMap<AActor*, const FRadarTrack*> DominantTracks;
	for (const FRadarTrack& Track : Tracks)
	{
		if (!Track.TrackedActor.IsValid())
		{
			continue;
		}

		AActor* TargetActor = Track.TrackedActor.Get();
		if (const FRadarTrack** Existing = DominantTracks.Find(TargetActor))
		{
			if (GetTrackPriority(Track.Status) > GetTrackPriority((*Existing)->Status))
			{
				DominantTracks[TargetActor] = &Track;
			}
		}
		else
		{
			DominantTracks.Add(TargetActor, &Track);
		}
	}

	for (const auto& Pair : DominantTracks)
	{
		if (Pair.Value)
		{
			FAircraftCombatDebug::DrawTrackSymbology(World, this, RadarPosition, *Pair.Value, true);
		}
	}
}
