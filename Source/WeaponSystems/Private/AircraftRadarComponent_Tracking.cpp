// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
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
	NewTrack.TrackedActor = DetectedActor;
	NewTrack.TrackAge = 0.0f;

	// In TWS mode, new contacts start as Tracked
	if (RadarMode == ERadarOperatingMode::TrackWhileScan)
	{
		NewTrack.Status = ERadarTrackStatus::Tracked;
	}
	else
	{
		NewTrack.Status = ERadarTrackStatus::Search;
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

	// Preserve track ID, status, and beam target flags
	const int32 SavedTrackID = Track.TrackID;
	const ERadarTrackStatus SavedStatus = Track.Status;
	const ERadarIFFResult SavedIFF = Track.IFFResult;
	const bool bSavedBeamTarget = Track.bIsBeamTarget;

	// Update measurement data
	Track.LastKnownPosition = NewDetection.LastKnownPosition;
	Track.EstimatedVelocity = SmoothVelocity(Track.EstimatedVelocity, NewDetection.EstimatedVelocity, 0.3f);
	Track.ClosureRate = NewDetection.ClosureRate;
	Track.Bearing = NewDetection.Bearing;
	Track.Elevation = NewDetection.Elevation;
	Track.Range = NewDetection.Range;
	Track.SignalStrength = NewDetection.SignalStrength;
	Track.AltitudeASL = NewDetection.AltitudeASL;
	Track.TargetHeading = NewDetection.TargetHeading;
	Track.bIsNotching = NewDetection.bIsNotching;
	Track.bIsJamming = NewDetection.bIsJamming;

	// Reset age (fresh return)
	Track.TrackAge = 0.0f;

	// Restore preserved fields
	Track.TrackID = SavedTrackID;
	Track.Status = SavedStatus;
	Track.IFFResult = SavedIFF;
	Track.bIsBeamTarget = bSavedBeamTarget;

	// If this track was a Search hit and we're in TWS, promote to Tracked
	if (Track.Status == ERadarTrackStatus::Search && RadarMode == ERadarOperatingMode::TrackWhileScan)
	{
		Track.Status = ERadarTrackStatus::Tracked;
	}

	// Broadcast notching warning
	if (Track.bIsNotching)
	{
		OnRadarTargetNotching.Broadcast(Track);
	}

	OnRadarContactUpdated.Broadcast(Track);
}

void UAircraftRadarComponent::UpdateTrackFiles(float DeltaTime)
{
	for (int32 i = Tracks.Num() - 1; i >= 0; --i)
	{
		FRadarTrack& Track = Tracks[i];

		// Check if tracked actor is still valid
		if (!Track.TrackedActor.IsValid())
		{
			const int32 LostTrackID = Track.TrackID;

			// Clear STT lock if this was the locked track
			if (Track.TrackID == STTLockedTrackID)
			{
				STTLockedTrackID = -1;
				OnRadarLockLost.Broadcast(LostTrackID);
			}
			if (Track.TrackID == BuggedTrackID)
			{
				BuggedTrackID = -1;
			}

			Tracks.RemoveAt(i);
			OnRadarContactLost.Broadcast(LostTrackID);
			continue;
		}

		// Update bearing/elevation/range from current actor position
		if (Track.TrackedActor.IsValid())
		{
			const AActor* OwnerActor = GetOwner();
			if (OwnerActor)
			{
				const FVector TargetPos = Track.TrackedActor->GetActorLocation();
				const FVector RadarPos = OwnerActor->GetActorLocation();
				const FVector ToTarget = TargetPos - RadarPos;

				Track.LastKnownPosition = TargetPos;
				Track.Range = ToTarget.Size();
				ComputeBearingElevation(TargetPos, Track.Bearing, Track.Elevation);

				// Update closure rate
				const FVector RadarVel = OwnerActor->GetVelocity();
				const FVector TargetVel = Track.TrackedActor->GetVelocity();
				const FVector R_Hat = ToTarget.GetSafeNormal();
				const FVector V_Rel = TargetVel - RadarVel;
				Track.ClosureRate = -FVector::DotProduct(R_Hat, V_Rel);

				// Update heading
				Track.TargetHeading = Track.TrackedActor->GetActorRotation().Yaw;
				Track.AltitudeASL = TargetPos.Z;

				// Smooth velocity
				Track.EstimatedVelocity = SmoothVelocity(Track.EstimatedVelocity, TargetVel, 0.2f);

				// Check Doppler notch
				if (NotchFilterVelocity > 0.0f)
				{
					Track.bIsNotching = FMath::Abs(Track.ClosureRate) < NotchFilterVelocity;
				}
			}
		}
	}
}

void UAircraftRadarComponent::PruneStaleTracks(float DeltaTime)
{
	// Scale timeout to at least 1.5x full frame sweep time so tracks never drop halfway through a multi-bar scan
	const float ScanFrameTime = (AzimuthScanWidth > 0.0f && ScanRateDegreesPerSecond > 0.0f)
		? (AzimuthScanWidth * FMath::Max(1, ElevationBars) / ScanRateDegreesPerSecond)
		: 5.0f;
	const float EffectiveDropTimeout = FMath::Max(TrackDropTimeout, ScanFrameTime * 1.5f);

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
		const int32 OldLockID = STTLockedTrackID;
		STTLockedTrackID = -1;
		OnRadarLockLost.Broadcast(OldLockID);
		SetRadarMode(PreSTTMode);
		return;
	}

	FRadarTrack& Track = Tracks[TrackIndex];

	// Verify target still valid
	if (!Track.TrackedActor.IsValid())
	{
		const int32 OldLockID = STTLockedTrackID;
		STTLockedTrackID = -1;
		Tracks.RemoveAt(TrackIndex);
		OnRadarLockLost.Broadcast(OldLockID);
		OnRadarContactLost.Broadcast(OldLockID);
		SetRadarMode(PreSTTMode);
		return;
	}

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
			const int32 OldLockID = STTLockedTrackID;
			STTLockedTrackID = -1;
			Track.Status = ERadarTrackStatus::Lost;
			Track.bIsBeamTarget = false;
			OnRadarLockLost.Broadcast(OldLockID);
			SetRadarMode(PreSTTMode);
			return;
		}

		// Check Doppler notch in STT — some radars can hold through notch in STT due to angle tracking
		if (Track.bIsNotching)
		{
			OnRadarTargetNotching.Broadcast(Track);

			if (bEnableDebugTraces && bEnableDiagnosticHUD)
			{
				FAircraftCombatDebug::PrintRadarTelemetry(3, TEXT("[RADAR STT] TARGET NOTCHING!"), FColor::Yellow, 0.5f);
			}
		}
	}
	else
	{
		// Lost detection — target may have gone to ground or out of range
		Track.TrackAge += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
		if (Track.TrackAge > TrackDropTimeout * 0.5f)
		{
			const int32 OldLockID = STTLockedTrackID;
			STTLockedTrackID = -1;
			Track.Status = ERadarTrackStatus::Lost;
			Track.bIsBeamTarget = false;
			OnRadarLockLost.Broadcast(OldLockID);
			SetRadarMode(PreSTTMode);
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

	const FVector RadarPosition = OwnerActor->GetActorLocation();
	const FRotator OwnerRotation = OwnerActor->GetActorRotation();
	const FVector ForwardDir = OwnerRotation.Vector();

	float AcquisitionConeAngle = ACMBoresightConeAngle;
	FVector AcquisitionDirection = ForwardDir;

	switch (ACMSubMode)
	{
		case ERadarACMSubMode::Boresight:
			AcquisitionConeAngle = ACMBoresightConeAngle;
			AcquisitionDirection = ForwardDir;
			break;

		case ERadarACMSubMode::VerticalScan:
			AcquisitionConeAngle = 5.0f; // Narrow horizontal, wide vertical
			AcquisitionDirection = ForwardDir;
			break;

		case ERadarACMSubMode::SlewAcquisition:
		{
			// TDC-steered: Use scan center offsets
			const FRotator SlewRotator(ScanCenterElevation, ScanCenterAzimuth, 0.0f);
			AcquisitionDirection = (OwnerRotation.Quaternion() * SlewRotator.Quaternion()).GetForwardVector();
			AcquisitionConeAngle = 10.0f;
			break;
		}

		case ERadarACMSubMode::HelmetCue:
			// Helmet cue would need external HMD look direction input
			// Fallback to boresight
			AcquisitionConeAngle = ACMBoresightConeAngle;
			AcquisitionDirection = ForwardDir;
			break;
	}

	const float CosCone = FMath::Cos(FMath::DegreesToRadians(AcquisitionConeAngle));

	// 2. Query potential targets: Subsystem registry first, then fallback to scene query
	TArray<AActor*> CandidateActors;
	if (UAircraftCombatSubsystem* Subsystem = UAircraftCombatSubsystem::Get(this))
	{
		Subsystem->GetCombatActorsInVolume(RadarPosition, ACMAutoLockRange, CandidateActors);
	}

	if (CandidateActors.Num() == 0)
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RadarACMAcquisition), false, OwnerActor);
		QueryParams.AddIgnoredActor(OwnerActor);
		TArray<AActor*> AttachedActors;
		OwnerActor->GetAttachedActors(AttachedActors, true, true);
		QueryParams.AddIgnoredActors(AttachedActors);

		TArray<FOverlapResult> OverlapResults;
		const FCollisionShape SphereShape = FCollisionShape::MakeSphere(ACMAutoLockRange);

		if (bQueryAllDynamicObjects)
		{
			FCollisionObjectQueryParams ObjectParams;
			ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
			ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
			ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
			ObjectParams.AddObjectTypesToQuery(ECC_Vehicle);

			World->OverlapMultiByObjectType(OverlapResults, RadarPosition, FQuat::Identity, ObjectParams, SphereShape, QueryParams);

			if (OverlapResults.IsEmpty())
			{
				World->OverlapMultiByChannel(OverlapResults, RadarPosition, FQuat::Identity, DetectionChannel, SphereShape, QueryParams);
			}
		}
		else
		{
			World->OverlapMultiByChannel(OverlapResults, RadarPosition, FQuat::Identity, DetectionChannel, SphereShape, QueryParams);
		}

		for (const FOverlapResult& Overlap : OverlapResults)
		{
			if (AActor* Candidate = Overlap.GetActor())
			{
				CandidateActors.AddUnique(Candidate);
			}
		}
	}

	if (CandidateActors.IsEmpty())
	{
		return;
	}

	// Find closest target within ACM cone
	AActor* BestTarget = nullptr;
	float BestRangeSq = TNumericLimits<float>::Max();

	TSet<AActor*> ProcessedActors;
	for (AActor* Candidate : CandidateActors)
	{
		if (!Candidate || ProcessedActors.Contains(Candidate) || Candidate == OwnerActor)
		{
			continue;
		}
		ProcessedActors.Add(Candidate);

		// Tag filtering (checks actor and component tags)
		if (!CheckCandidateTags(Candidate))
		{
			continue;
		}

		const FVector ToTarget = Candidate->GetActorLocation() - RadarPosition;
		const float RangeSq = ToTarget.SizeSquared();
		const FVector ToTargetDir = ToTarget.GetSafeNormal();
		const float DotProduct = FVector::DotProduct(AcquisitionDirection, ToTargetDir);

		if (DotProduct >= CosCone && RangeSq < BestRangeSq)
		{
			// Terrain masking check
			if (bEnableTerrainMasking && IsTerrainMasked(RadarPosition, Candidate->GetActorLocation(), Candidate))
			{
				continue;
			}

			BestRangeSq = RangeSq;
			BestTarget = Candidate;
		}
	}

	if (BestTarget)
	{
		// Auto-lock the first target found in ACM
		FRadarTrack RawTrack;
		if (EvaluateCandidate(BestTarget, RawTrack))
		{
			const int32 NewTrackID = CreateTrack(BestTarget, RawTrack);
			CommandLock(NewTrackID);
		}
	}

	// Debug: Draw ACM cone
	if (bEnableDebugTraces && bDrawAntennaBeam)
	{
		const float ConeAngleRad = FMath::DegreesToRadians(AcquisitionConeAngle);
		DrawDebugCone(World, RadarPosition, AcquisitionDirection, ACMAutoLockRange * 0.3f, ConeAngleRad, ConeAngleRad, 16, FColor::Orange, false, -1.0f, 0, 1.5f);
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

	const FVector RadarPosition = OwnerActor->GetActorLocation();

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
			FAircraftCombatDebug::DrawTrackSymbology(World, RadarPosition, *Pair.Value, true);
		}
	}
}
