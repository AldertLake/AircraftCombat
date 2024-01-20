// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------
//
// AircraftRadarComponent_Scan.cpp — Antenna sweep simulation, radar detection model, and candidate evaluation
//

#include "AircraftRadarComponent.h"
#include "AircraftCombatSubsystem.h"
#include "AircraftCombatDebug.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"

void UAircraftRadarComponent::PerformScanSweep(float DeltaTime)
{
	UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	// 1. Advance antenna azimuth position
	const float SweepDelta = ScanRateDegreesPerSecond * DeltaTime;
	const float ScanLeft = ScanCenterAzimuth - (AzimuthScanWidth * 0.5f);
	const float ScanRight = ScanCenterAzimuth + (AzimuthScanWidth * 0.5f);

	if (bScanningRight)
	{
		CurrentScanAzimuth += SweepDelta;
		if (CurrentScanAzimuth >= ScanRight)
		{
			CurrentScanAzimuth = ScanRight;
			bScanningRight = false;

			// Advance to next elevation bar
			CurrentScanBar++;
			if (CurrentScanBar >= ElevationBars)
			{
				CurrentScanBar = 0;
				OnScanSweepComplete.Broadcast();
			}
		}
	}
	else
	{
		CurrentScanAzimuth -= SweepDelta;
		if (CurrentScanAzimuth <= ScanLeft)
		{
			CurrentScanAzimuth = ScanLeft;
			bScanningRight = true;

			// Advance to next elevation bar
			CurrentScanBar++;
			if (CurrentScanBar >= ElevationBars)
			{
				CurrentScanBar = 0;
				OnScanSweepComplete.Broadcast();
			}
		}
	}

	// 2. Compute beam direction and elevation parameters
	const FVector RadarPosition = OwnerActor->GetActorLocation();

	// Calculate current beam elevation angle based on bar position
	const float ElevationStep = (ElevationBars > 1) ? ElevationScanHeight / static_cast<float>(ElevationBars - 1) : 0.0f;
	const float BarElevation = ScanCenterElevation - (ElevationScanHeight * 0.5f) + (ElevationStep * CurrentScanBar);

	// 3. Target candidates: subsystem registry + fallback scene query
	TArray<AActor*> CandidateActors;
	const float ScanRange = FMath::Min(MaxDetectionRange, CurrentDisplayRange * 1.5f);

	if (UAircraftCombatSubsystem* Subsystem = UAircraftCombatSubsystem::Get(this))
	{
		Subsystem->GetCombatActorsInVolume(RadarPosition, ScanRange, CandidateActors);
	}

	// Fallback to physics query if no registered combat actors are in the world
	if (CandidateActors.Num() == 0)
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RadarScanOverlap), false, OwnerActor);
		QueryParams.AddIgnoredActor(OwnerActor);
		TArray<AActor*> AttachedActors;
		OwnerActor->GetAttachedActors(AttachedActors, true, true);
		QueryParams.AddIgnoredActors(AttachedActors);

		TArray<FOverlapResult> OverlapResults;
		const FCollisionShape SphereShape = FCollisionShape::MakeSphere(ScanRange);

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

	// 4. Evaluate candidates against radar parameters and antenna beam
	TSet<AActor*> ProcessedActors;
	ProcessedActors.Reserve(CandidateActors.Num());

	const float HalfAz = AzimuthScanWidth * 0.5f;
	const float HalfEl = ElevationScanHeight * 0.5f;

	// Overlapping elevation beam half-width: covers bar step with overlap margin
	const float HalfElStep = (ElevationBars > 1) ? (ElevationStep * 0.65f) : (ElevationScanHeight * 0.5f);
	const float EffectiveBeamElHalf = FMath::Max(BeamElevationWidth * 0.5f, HalfElStep);

	// Sweeping azimuth beam half-width: accounts for frame sweep delta
	const float EffectiveBeamAzHalf = FMath::Max(BeamAzimuthWidth * 0.5f, SweepDelta * 1.5f);

	for (AActor* Candidate : CandidateActors)
	{
		if (!Candidate || ProcessedActors.Contains(Candidate) || Candidate == OwnerActor)
		{
			continue;
		}
		ProcessedActors.Add(Candidate);

		// Tag filtering (checks both actor tags and component tags)
		if (!CheckCandidateTags(Candidate))
		{
			continue;
		}

		const FVector TargetPosition = Candidate->GetActorLocation();

		// Compute candidate local bearing and elevation relative to radar nose
		float TargetBearing = 0.0f;
		float TargetElevation = 0.0f;
		ComputeBearingElevation(TargetPosition, TargetBearing, TargetElevation);

		// Check total scan volume bounds
		const float TargetAzDiff = FMath::Abs(FMath::FindDeltaAngleDegrees(ScanCenterAzimuth, TargetBearing));
		const float TargetElDiff = FMath::Abs(TargetElevation - ScanCenterElevation);
		const bool bInScanVolume = (TargetAzDiff <= HalfAz && TargetElDiff <= HalfEl);

		if (!bInScanVolume)
		{
			continue;
		}

		// Evaluate antenna beam intersection
		const float ElDiff = FMath::Abs(TargetElevation - BarElevation);
		const bool bInBeamElevation = (ElDiff <= EffectiveBeamElHalf);

		const float AzDiff = FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentScanAzimuth, TargetBearing));
		const bool bInBeamAzimuth = (AzDiff <= EffectiveBeamAzHalf);

		const bool bInActiveBeam = bInstantFullVolumeScan ? true : (bInBeamAzimuth && bInBeamElevation);

		if (!bInActiveBeam)
		{
			continue;
		}

		// Evaluate candidate against radar equation, ranges, Doppler notch, terrain masking
		FRadarTrack RawTrack;
		if (!EvaluateCandidate(Candidate, RawTrack))
		{
			continue;
		}

		// TARGET DETECTED! Update or create track file
		const int32 ExistingTrackIndex = FindTrackIndexByActor(Candidate);
		if (ExistingTrackIndex != INDEX_NONE)
		{
			UpdateTrack(ExistingTrackIndex, RawTrack);
		}
		else if (RadarMode == ERadarOperatingMode::TrackWhileScan && Tracks.Num() >= MaxSimultaneousTWSTracks)
		{
			// TWS track limit reached — skip new contacts
			continue;
		}
		else
		{
			CreateTrack(Candidate, RawTrack);
		}
	}
}

bool UAircraftRadarComponent::CheckCandidateTags(const AActor* Candidate, FString* OutFoundTags) const
{
	if (!IsValid(Candidate))
	{
		return false;
	}

	TArray<FName> FoundTagNames = Candidate->Tags;

	// Also check component tags
	if (bSearchComponentTags)
	{
		TInlineComponentArray<UActorComponent*> Components(Candidate);
		for (const UActorComponent* Comp : Components)
		{
			if (Comp)
			{
				for (const FName& CompTag : Comp->ComponentTags)
				{
					FoundTagNames.AddUnique(CompTag);
				}
			}
		}
	}

	if (OutFoundTags)
	{
		TArray<FString> TagStrings;
		for (const FName& T : FoundTagNames)
		{
			TagStrings.Add(T.ToString());
		}
		*OutFoundTags = FString::Join(TagStrings, TEXT(", "));
	}

	if (DetectableActorTags.Num() == 0)
	{
		return true; // No filter configured -> all candidates accepted
	}

	for (const FName& RequiredTag : DetectableActorTags)
	{
		if (FoundTagNames.Contains(RequiredTag))
		{
			return true;
		}
	}

	return false;
}

bool UAircraftRadarComponent::EvaluateCandidate(AActor* Candidate, FRadarTrack& OutTrack, FString* OutRejectReason) const
{
	if (!IsValid(Candidate))
	{
		if (OutRejectReason) *OutRejectReason = TEXT("Candidate actor is invalid");
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		if (OutRejectReason) *OutRejectReason = TEXT("Owner actor is invalid");
		return false;
	}

	const FVector RadarPosition = OwnerActor->GetActorLocation();
	const FVector TargetPosition = Candidate->GetActorLocation();
	const FVector ToTarget = TargetPosition - RadarPosition;
	const float Range = ToTarget.Size();

	// Range checks
	if (Range < MinDetectionRange)
	{
		if (OutRejectReason) *OutRejectReason = FString::Printf(TEXT("Inside Min Range (%.0f m < %.0f m)"), Range / 100.0f, MinDetectionRange / 100.0f);
		return false;
	}
	if (Range > MaxDetectionRange)
	{
		if (OutRejectReason) *OutRejectReason = FString::Printf(TEXT("Beyond Max Range (%.1f km > %.1f km)"), Range / 100000.0f, MaxDetectionRange / 100000.0f);
		return false;
	}

	// Radar equation detection range based on RCS
	const float TargetRCS = DefaultTargetRCS;
	const float DetectionRange = CalculateDetectionRange(TargetRCS);
	if (Range > DetectionRange)
	{
		if (OutRejectReason) *OutRejectReason = FString::Printf(TEXT("RCS insufficient for distance (RCS: %.1f m², MaxR: %.1f km, Dist: %.1f km)"), TargetRCS, DetectionRange / 100000.0f, Range / 100000.0f);
		return false;
	}

	// Doppler notch check — compute closure velocity
	const FVector RadarVelocity = OwnerActor->GetVelocity();
	const FVector TargetVelocity = Candidate->GetVelocity();
	const FVector R_Hat = ToTarget.GetSafeNormal();
	const FVector V_Rel = TargetVelocity - RadarVelocity;
	const float ClosureRate = -FVector::DotProduct(R_Hat, V_Rel);

	bool bIsNotching = false;
	if (NotchFilterVelocity > 0.0f && FMath::Abs(ClosureRate) < NotchFilterVelocity)
	{
		if (RadarMode != ERadarOperatingMode::GroundMapping && RadarMode != ERadarOperatingMode::SeaSearch)
		{
			bIsNotching = true;
		}
	}

	// Terrain masking check
	if (bEnableTerrainMasking)
	{
		FHitResult MaskHit;
		if (IsTerrainMasked(RadarPosition, TargetPosition, Candidate, &MaskHit))
		{
			const FString BlockerName = MaskHit.GetActor() ? MaskHit.GetActor()->GetName() : TEXT("Static Geometry");
			if (OutRejectReason) *OutRejectReason = FString::Printf(TEXT("Terrain Masked by '%s'"), *BlockerName);
			return false;
		}
	}

	// Populate raw track data
	float Bearing, Elevation;
	ComputeBearingElevation(TargetPosition, Bearing, Elevation);

	const float NormRange = FMath::Clamp(Range / MaxDetectionRange, 0.0f, 1.0f);
	const float SignalStrength = FMath::Clamp(1.0f - FMath::Pow(NormRange, 4.0f), 0.0f, 1.0f);

	OutTrack = FRadarTrack();
	OutTrack.TrackedActor = Candidate;
	OutTrack.Status = (RadarMode == ERadarOperatingMode::TrackWhileScan) ? ERadarTrackStatus::Tracked : ERadarTrackStatus::Search;
	OutTrack.LastKnownPosition = TargetPosition;
	OutTrack.EstimatedVelocity = TargetVelocity;
	OutTrack.ClosureRate = ClosureRate;
	OutTrack.Bearing = Bearing;
	OutTrack.Elevation = Elevation;
	OutTrack.Range = Range;
	OutTrack.SignalStrength = SignalStrength;
	OutTrack.TrackAge = 0.0f;
	OutTrack.AltitudeASL = TargetPosition.Z;
	OutTrack.TargetHeading = Candidate->GetActorRotation().Yaw;
	OutTrack.bIsNotching = bIsNotching;
	OutTrack.bIsJamming = false;
	OutTrack.bIsBeamTarget = false;

	return true;
}

bool UAircraftRadarComponent::IsTerrainMasked(const FVector& RadarPosition, const FVector& TargetPosition, const AActor* TargetActor, FHitResult* OutHit) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RadarTerrainMask), true);
	if (OwnerActor)
	{
		TraceParams.AddIgnoredActor(OwnerActor);
		TArray<AActor*> Attached;
		OwnerActor->GetAttachedActors(Attached, true, true);
		TraceParams.AddIgnoredActors(Attached);
	}

	// CRITICAL FIX: Ignore the target candidate actor so its own static mesh collision doesn't mask itself!
	if (TargetActor)
	{
		TraceParams.AddIgnoredActor(TargetActor);
		TArray<AActor*> TargetAttached;
		const_cast<AActor*>(TargetActor)->GetAttachedActors(TargetAttached, true, true);
		TraceParams.AddIgnoredActors(TargetAttached);
	}

	const FVector ToTarget = TargetPosition - RadarPosition;
	const float TotalDist = ToTarget.Size();
	if (TotalDist < 300.0f)
	{
		return false; // Point-blank range
	}

	const FVector Dir = ToTarget / TotalDist;
	const FVector TraceStart = RadarPosition + Dir * 150.0f; // Clear nose radome
	const FVector TraceEnd = TargetPosition - Dir * 100.0f;  // End short of target

	FHitResult HitResult;
	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_WorldStatic,
		TraceParams
	);

	if (OutHit)
	{
		*OutHit = HitResult;
	}

	if (bHit && HitResult.GetActor() != nullptr)
	{
		return true;
	}

	return false;
}

void UAircraftRadarComponent::ComputeBearingElevation(const FVector& TargetPosition, float& OutBearing, float& OutElevation) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		OutBearing = 0.0f;
		OutElevation = 0.0f;
		return;
	}

	const FVector RadarPosition = OwnerActor->GetActorLocation();
	const FRotator OwnerRotation = OwnerActor->GetActorRotation();
	const FVector ToTarget = TargetPosition - RadarPosition;

	// Transform to local space (relative to aircraft nose)
	const FVector LocalDir = OwnerRotation.UnrotateVector(ToTarget.GetSafeNormal());
	const FRotator LocalRot = LocalDir.Rotation();

	OutBearing = LocalRot.Yaw;
	OutElevation = LocalRot.Pitch;
}

void UAircraftRadarComponent::DrawDebugScanVolume() const
{
	UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	const FVector RadarPosition = OwnerActor->GetActorLocation();
	const FRotator OwnerRotation = OwnerActor->GetActorRotation();
	const float VisRange = FMath::Clamp(CurrentDisplayRange * 0.15f, 20000.0f, 300000.0f);

	if (bDrawScanVolume)
	{
		FAircraftCombatDebug::DrawRadarFrustum(
			World,
			RadarPosition,
			OwnerRotation,
			AzimuthScanWidth,
			ElevationScanHeight,
			ScanCenterAzimuth,
			ScanCenterElevation,
			ElevationBars,
			CurrentScanBar,
			VisRange
		);
	}

	if (bDrawAntennaBeam)
	{
		const float ElevationStep = (ElevationBars > 1) ? ElevationScanHeight / static_cast<float>(ElevationBars - 1) : 0.0f;
		const float CurrentBarElevation = ScanCenterElevation - (ElevationScanHeight * 0.5f) + (ElevationStep * CurrentScanBar);

		FAircraftCombatDebug::DrawAntennaBeam(
			World,
			RadarPosition,
			OwnerRotation,
			CurrentScanAzimuth,
			CurrentBarElevation,
			BeamAzimuthWidth * 0.5f,
			BeamElevationWidth * 0.5f,
			VisRange,
			true
		);
	}
}
