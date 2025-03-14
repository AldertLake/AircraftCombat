// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------
//
// AircraftRadarComponent_Scan.cpp — Antenna sweep simulation, radar detection model, and candidate evaluation
//

#include "AircraftRadarComponent.h"
#include "IFFTransponderComponent.h"
#include "AircraftCombatSubsystem.h"
#include "AircraftCombatDebug.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogAircraftRadarScan, Log, All);

void UAircraftRadarComponent::PerformScanSweep(float DeltaTime)
{
	UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	const ERadarScanDrive Drive = ScanDrive;
	const FVector RadarPosition = GetRadarLocation();
	ActiveSampleBeams.Reset();
	SampleSweepAzHalf = 0.0f;
	SampleSweepElHalf = 0.0f;
	float EffectiveBeamAzHalf = FMath::Max(0.5f, BeamAzimuthWidth * 0.5f);
	float EffectiveBeamElHalf = FMath::Max(0.5f, BeamElevationWidth * 0.5f);

	if (Drive == ERadarScanDrive::SocketDriven)
	{
		FVector SourceLocation, ReferenceLocation;
		FRotator SourceRotation, ReferenceRotation;
		GetRadarSourceTransform(SourceLocation, SourceRotation);
		GetRadarReferenceTransform(ReferenceLocation, ReferenceRotation);
		const FVector CurrentLocalForward = ReferenceRotation.UnrotateVector(SourceRotation.Vector());
		const FRotator CurrentLocal = CurrentLocalForward.Rotation();
		const FRotator PreviousLocal = PreviousSocketLocalForward.IsNearlyZero()
			? CurrentLocal : PreviousSocketLocalForward.Rotation();
		const float SweptAz = FMath::FindDeltaAngleDegrees(PreviousLocal.Yaw, CurrentLocal.Yaw);
		const float SweptEl = CurrentLocal.Pitch - PreviousLocal.Pitch;
		ActiveSampleBeams.Add(FVector2D(PreviousLocal.Yaw + SweptAz * 0.5f,
			PreviousLocal.Pitch + SweptEl * 0.5f));
		EffectiveBeamAzHalf += FMath::Abs(SweptAz) * 0.5f;
		EffectiveBeamElHalf += FMath::Abs(SweptEl) * 0.5f;
		SampleSweepAzHalf = FMath::Abs(SweptAz) * 0.5f;
		SampleSweepElHalf = FMath::Abs(SweptEl) * 0.5f;
		PreviousSocketLocalForward = CurrentLocalForward;
		SocketSweepDegrees += SweptAz;
		if (FMath::Abs(SocketSweepDegrees) >= 360.0f)
		{
			SocketSweepDegrees = FMath::Fmod(SocketSweepDegrees, 360.0f);
			++ScanSweepCounter;
			OnScanSweepComplete.Broadcast();
		}
		CurrentScanAzimuth = CurrentLocal.Yaw;
		CurrentScanBar = 0;
		EmitScanProgress(0, 0.5f, FMath::Abs(SocketSweepDegrees) / 360.0f, SweptAz >= 0.0f);
	}
	else if (Drive == ERadarScanDrive::VirtualMechanical && ScanRateDegreesPerSecond > 0.0f)
	{
		const float OldAzimuth = CurrentScanAzimuth;
		AdvanceAntennaSweep(DeltaTime, true);
		const float SweptAz = FMath::FindDeltaAngleDegrees(OldAzimuth, CurrentScanAzimuth);
		const float ElevationStep = ElevationBars > 1 ? ElevationScanHeight / (ElevationBars - 1) : 0.0f;
		const float BarElevation = ElevationBars > 1
			? ScanCenterElevation - ElevationScanHeight * 0.5f + ElevationStep * CurrentScanBar : ScanCenterElevation;
		ActiveSampleBeams.Add(FVector2D(OldAzimuth + SweptAz * 0.5f, BarElevation));
		EffectiveBeamAzHalf += FMath::Abs(SweptAz) * 0.5f;
		SampleSweepAzHalf = FMath::Abs(SweptAz) * 0.5f;
		EffectiveBeamElHalf = FMath::Max(EffectiveBeamElHalf, ElevationStep * 0.5f);
	}
	else
	{
		// PESA visits one beam at a time. AESA visits several independently steered beams per sample.
		const int32 AzCells = FMath::Max(1, FMath::CeilToInt(AzimuthScanWidth / FMath::Max(BeamAzimuthWidth, 1.0f)));
		const int32 Bars = FMath::Max(1, ElevationBars);
		const int32 TotalCells = AzCells * Bars;
		const int32 Visits = Drive == ERadarScanDrive::AESA ? FMath::Clamp(AESABeamsPerSample, 1, 32) : 1;
		for (int32 Visit = 0; Visit < Visits; ++Visit)
		{
			const int32 Cell = ElectronicBeamIndex % TotalCells;
			const int32 AzCell = Cell % AzCells;
			const int32 Bar = Cell / AzCells;
			const float Az = ScanCenterAzimuth - AzimuthScanWidth * 0.5f +
				(AzCell + 0.5f) * AzimuthScanWidth / AzCells;
			const float El = ScanCenterElevation - ElevationScanHeight * 0.5f +
				(Bar + 0.5f) * ElevationScanHeight / Bars;
			ActiveSampleBeams.Add(FVector2D(Az, El));
			CurrentScanAzimuth = Az;
			CurrentScanBar = Bar;
			ElectronicBeamIndex = (ElectronicBeamIndex + 1) % TotalCells;
			if (ElectronicBeamIndex == 0)
			{
				++ScanSweepCounter;
				OnScanSweepComplete.Broadcast();
			}
		}
		EffectiveBeamAzHalf = FMath::Max(EffectiveBeamAzHalf, AzimuthScanWidth / (AzCells * 2.0f));
		EffectiveBeamElHalf = FMath::Max(EffectiveBeamElHalf, ElevationScanHeight / (Bars * 2.0f));
		EmitScanProgress(CurrentScanBar,
			FMath::Clamp((CurrentScanAzimuth - ScanCenterAzimuth + AzimuthScanWidth * 0.5f) / FMath::Max(AzimuthScanWidth, 1.0f), 0.0f, 1.0f),
			static_cast<float>(ElectronicBeamIndex) / TotalCells, true);
	}

	// Search range belongs to the sensor, not to a pilot's display scale.
	TArray<AActor*> CandidateActors;
	GatherCandidateActors(RadarPosition, MaxDetectionRange, CandidateActors);

	if (CandidateActors.IsEmpty())
	{
		return;
	}

	TSet<AActor*> ProcessedActors;
	ProcessedActors.Reserve(CandidateActors.Num());
	const APawn* DebugPawn = Cast<APawn>(OwnerActor);
	const bool bDebugThisSample = bEnableDebugTraces && bDebugCandidateRejections &&
		(!bDebugOnlyPlayerControlled || !DebugPawn || DebugPawn->IsLocallyControlled()) &&
		World->GetTimeSeconds() - LastCandidateDebugTime >= 0.5f;
	int32 DebuggedCandidates = 0;
	auto DebugReject = [&](const AActor* Candidate, const TCHAR* Reason)
	{
		if (!bDebugThisSample || !IsValid(Candidate) || DebuggedCandidates++ >= 8) return;
		DrawDebugLine(World, RadarPosition, Candidate->GetActorLocation(), FColor::Red, false, 0.55f, 0, 0.8f);
		DrawDebugString(World, Candidate->GetActorLocation() + FVector(0, 0, 150),
			FString::Printf(TEXT("%s: %s"), *GetNameSafe(OwnerActor), Reason), nullptr,
			FColor::Red, 0.55f, false);
		UE_LOG(LogAircraftRadarScan, Verbose, TEXT("%s rejected %s: %s"),
			*GetNameSafe(OwnerActor), *GetNameSafe(Candidate), Reason);
	};

	const float HalfAz = AzimuthScanWidth * 0.5f;
	const float HalfEl = ElevationScanHeight * 0.5f;

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
			DebugReject(Candidate, TEXT("Tag filter"));
			continue;
		}

		// Domain filtering for active scan mode (Air, Ground, Sea)
		const ERadarTargetDomain CandidateDomain = ResolveCandidateDomain(Candidate);
		if (!IsDomainAllowedForMode(CandidateDomain, RadarMode))
		{
			DebugReject(Candidate, TEXT("Domain filter"));
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
		const bool bInScanVolume = Drive == ERadarScanDrive::SocketDriven ||
			((AzimuthScanWidth >= 360.0f || TargetAzDiff <= HalfAz) && TargetElDiff <= HalfEl);

		if (!bInScanVolume)
		{
			DebugReject(Candidate, TEXT("Outside scan sector"));
			continue;
		}

		bool bInSampledBeam = false;
		for (const FVector2D& Beam : ActiveSampleBeams)
		{
			if (FMath::Abs(TargetElevation - Beam.Y) <= EffectiveBeamElHalf &&
				FMath::Abs(FMath::FindDeltaAngleDegrees(Beam.X, TargetBearing)) <= EffectiveBeamAzHalf)
			{
				bInSampledBeam = true;
				break;
			}
		}
		if (!bInSampledBeam)
		{
			DebugReject(Candidate, TEXT("Outside live beam"));
			continue;
		}

		// Evaluate candidate against radar equation, ranges, Doppler notch, terrain masking
		FRadarTrack RawTrack;
		FString RejectReason;
		if (!EvaluateCandidate(Candidate, RawTrack, bDebugThisSample ? &RejectReason : nullptr))
		{
			if (RawTrack.bIsNotching)
			{
				const int32 ExistingIndex = FindTrackIndexByActor(Candidate);
				if (ExistingIndex != INDEX_NONE && !Tracks[ExistingIndex].bIsNotching)
				{
					Tracks[ExistingIndex].bIsNotching = true;
					OnRadarTargetNotching.Broadcast(Tracks[ExistingIndex]);
				}
			}
			DebugReject(Candidate, RejectReason.IsEmpty() ? TEXT("Detection model") : *RejectReason);
			continue;
		}

		// TARGET DETECTED! Update or create track file
		const int32 ExistingTrackIndex = FindTrackIndexByActor(Candidate);
		if (ExistingTrackIndex != INDEX_NONE)
		{
			UpdateTrack(ExistingTrackIndex, RawTrack);
		}
		else if (Tracks.Num() >= MaxTrackFiles ||
			(RadarMode == ERadarOperatingMode::TrackWhileScan && Tracks.Num() >= MaxSimultaneousTWSTracks))
		{
			// TWS track limit reached — skip new contacts
			continue;
		}
		else
		{
			CreateTrack(Candidate, RawTrack);
		}
	}
	if (bDebugThisSample) LastCandidateDebugTime = World->GetTimeSeconds();

}

bool UAircraftRadarComponent::CheckCandidateTags(const AActor* Candidate, FString* OutFoundTags) const
{
	if (!IsValid(Candidate))
	{
		return false;
	}

	if (DetectableActorTags.Num() == 0)
	{
		return true; // No filter configured -> all candidates accepted
	}

	// Fast path: inspect actor tags directly without allocating dynamic arrays
	for (const FName& RequiredTag : DetectableActorTags)
	{
		if (Candidate->ActorHasTag(RequiredTag))
		{
			if (OutFoundTags)
			{
				*OutFoundTags = RequiredTag.ToString();
			}
			return true;
		}
	}

	// Check component tags if enabled
	if (bSearchComponentTags)
	{
		TInlineComponentArray<UActorComponent*> Components(Candidate);
		for (const UActorComponent* Comp : Components)
		{
			if (Comp)
			{
				for (const FName& RequiredTag : DetectableActorTags)
				{
					if (Comp->ComponentHasTag(RequiredTag))
					{
						if (OutFoundTags)
						{
							*OutFoundTags = RequiredTag.ToString();
						}
						return true;
					}
				}
			}
		}
	}

	// If DetectableActorTags contains "Air", check if candidate resolves to Air domain (including untagged default)
	static const FName TagAir(TEXT("Air"));
	static const FName TagAirCaps(TEXT("AIR"));
	if (DetectableActorTags.Contains(TagAir) || DetectableActorTags.Contains(TagAirCaps))
	{
		if (ResolveCandidateDomain(Candidate) == ERadarTargetDomain::Air)
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

	const FVector RadarPosition = GetRadarLocation();
	const FVector TargetPosition = Candidate->GetActorLocation();
	const FVector ToTarget = TargetPosition - RadarPosition;
	const float Range = ToTarget.Size();

	// Operational domain check (Air, Ground, Sea) against current radar operating mode
	const ERadarTargetDomain TargetDomain = ResolveCandidateDomain(Candidate);
	if (!IsDomainAllowedForMode(TargetDomain, RadarMode))
	{
		if (OutRejectReason)
		{
			const UEnum* DomainEnum = StaticEnum<ERadarTargetDomain>();
			const FString DomainName = DomainEnum ? DomainEnum->GetDisplayNameTextByValue(static_cast<int64>(TargetDomain)).ToString() : TEXT("Unknown");
			*OutRejectReason = FString::Printf(TEXT("Domain '%s' not accepted in current radar mode"), *DomainName);
		}
		return false;
	}

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

	// Radar equation detection range based on resolved target RCS and aspect angle
	bool bFoundRCSTag = false;
	FString MatchedRCSTag;
	float TargetRCS = DefaultTargetRCS;

	// Optimization: if target is already known in track files, reuse its cached base RCS to avoid per-frame tag parsing
	const int32 ExistingTrackIdx = FindTrackIndexByActor(Candidate);
	if (!bEnableTargetRCSTagParsing)
	{
		TargetRCS = DefaultTargetRCS;
	}
	else if (ExistingTrackIdx != INDEX_NONE && Tracks.IsValidIndex(ExistingTrackIdx))
	{
		TargetRCS = Tracks[ExistingTrackIdx].RCS;
		bFoundRCSTag = Tracks[ExistingTrackIdx].bHasRCSTag;
	}
	else
	{
		TargetRCS = ResolveTargetRCS(Candidate, bFoundRCSTag, MatchedRCSTag);
	}

	const FVector TargetToRadar = (RadarPosition - TargetPosition).GetSafeNormal();
	const float EffectiveRCS = CalculateEffectiveRCS(TargetRCS, Candidate, TargetToRadar);
	const float DetectionRange = CalculateDetectionRange(EffectiveRCS);
	if (Range > DetectionRange)
	{
		if (OutRejectReason)
		{
			*OutRejectReason = FString::Printf(TEXT("RCS insufficient for distance (Base RCS: %.4f m², Eff RCS: %.4f m²%s, MaxR: %.1f km, Dist: %.1f km)"),
				TargetRCS,
				EffectiveRCS,
				bFoundRCSTag ? TEXT("") : TEXT(" [UNTAGGED]"),
				DetectionRange / 100000.0f,
				Range / 100000.0f);
		}
		return false;
	}

	// Doppler notch check — compute closure velocity
	const FVector RadarVelocity = OwnerActor->GetVelocity();
	const FVector TargetVelocity = Candidate->GetVelocity();
	const FVector R_Hat = ToTarget.GetSafeNormal();
	const FVector V_Rel = TargetVelocity - RadarVelocity;
	const float ClosureRate = -FVector::DotProduct(R_Hat, V_Rel);

	bool bIsNotching = false;
	const bool bLookDownIntoClutter = TargetPosition.Z < RadarPosition.Z - 5000.0f;
	if (bLookDownIntoClutter && NotchFilterVelocity > 0.0f && FMath::Abs(ClosureRate) < NotchFilterVelocity)
	{
		// Surface targets (Ground or Sea) and surface radar modes bypass Doppler notch
		const bool bIsSurfaceTarget = (TargetDomain == ERadarTargetDomain::Ground || TargetDomain == ERadarTargetDomain::Sea);
		if (RadarMode != ERadarOperatingMode::GroundMapping && RadarMode != ERadarOperatingMode::SeaSearch && !bIsSurfaceTarget)
		{
			bIsNotching = true;
		}
	}
	if (bIsNotching && !(RadarMode == ERadarOperatingMode::SingleTargetTrack && bSTTAngleTrackThroughNotch))
	{
		OutTrack = FRadarTrack();
		OutTrack.bIsNotching = true;
		if (OutRejectReason) *OutRejectReason = TEXT("Doppler notch against look-down clutter");
		return false;
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

	// Compute local target direction in antenna coordinate space for beam pattern gain
	FVector AntennaLoc;
	FRotator AntennaRot;
	GetRadarSourceTransform(AntennaLoc, AntennaRot);
	const FVector LocalTargetDir = AntennaRot.UnrotateVector(ToTarget.GetSafeNormal());

	const float BeamGain = CalculateAntennaBeamGain(Bearing, Elevation, LocalTargetDir);
	if (BeamGain <= KINDA_SMALL_NUMBER || Range > DetectionRange * FMath::Sqrt(BeamGain))
	{
		if (OutRejectReason) *OutRejectReason = TEXT("Below beam-adjusted detection threshold");
		return false;
	}
	const float SignalStrength = CalculateSignalStrength(Range, EffectiveRCS, BeamGain);

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
	OutTrack.TargetHeading = CalculateHeadingFromVelocity(TargetVelocity, 0.0f);
	OutTrack.RCS = TargetRCS;
	OutTrack.EffectiveRCS = EffectiveRCS;
	OutTrack.bHasRCSTag = bFoundRCSTag;
	OutTrack.bIsNotching = bIsNotching;
	OutTrack.bIsJamming = false;
	OutTrack.bIsBeamTarget = false;
	OutTrack.TargetDomain = TargetDomain;
	OutTrack.bIsGroundTarget = (TargetDomain == ERadarTargetDomain::Ground || TargetDomain == ERadarTargetDomain::Sea);
	if (OutTrack.bIsGroundTarget)
	{
		const float TargetGroundSpeed = FVector(TargetVelocity.X, TargetVelocity.Y, 0.0f).Size();
		OutTrack.bIsGMTIMoving = (TargetGroundSpeed >= GMTIVelocityThreshold);
	}
	else
	{
		OutTrack.bIsGMTIMoving = false;
	}

	// IFF classification via IGenericTeamAgentInterface
	OutTrack.IFFResult = ClassifyIFF(Candidate);
	if (bIsNotching && RadarMode == ERadarOperatingMode::SingleTargetTrack &&
		bSTTAngleTrackThroughNotch && Tracks.IsValidIndex(ExistingTrackIdx))
	{
		// Angle remains measurable, but Doppler range is carried forward from the last return.
		const FRadarTrack& Prior = Tracks[ExistingTrackIdx];
		const float PredictedRange = FMath::Clamp(Prior.Range - Prior.ClosureRate * Prior.TrackAge,
			MinDetectionRange, MaxDetectionRange);
		OutTrack.LastKnownPosition = RadarPosition + R_Hat * PredictedRange;
		OutTrack.Range = PredictedRange;
		OutTrack.ClosureRate = Prior.ClosureRate;
		OutTrack.AltitudeASL = OutTrack.LastKnownPosition.Z;
		OutTrack.TargetHeading = Prior.TargetHeading;
		OutTrack.SignalStrength *= 0.5f;
	}

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

	// Ignore target candidate actor and attached components to prevent self-occlusion
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

	if (bHit && HitResult.bBlockingHit)
	{
		// Guard against false ground self-masking at point of origin on ground-placed radars
		if (HitResult.Distance < 250.0f && HitResult.ImpactNormal.Z > 0.5f)
		{
			return false;
		}
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

	FVector RadarPosition;
	FRotator RadarRotation;
	GetRadarSourceTransform(RadarPosition, RadarRotation);
	const FVector ToTarget = TargetPosition - RadarPosition;

	// Bearings remain stable while a mesh-driven socket rotates the live beam.
	FVector ReferenceLocation;
	FRotator ReferenceRotation;
	GetRadarReferenceTransform(ReferenceLocation, ReferenceRotation);
	const FVector LocalDir = ReferenceRotation.UnrotateVector(ToTarget.GetSafeNormal());
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

	FVector RadarPosition;
	FRotator RadarRotation;
	GetRadarSourceTransform(RadarPosition, RadarRotation);

	// 1. Air Combat Maneuver (ACM) mode visualization
	if (RadarMode == ERadarOperatingMode::AirCombatManeuver)
	{
		if (!bDrawScanVolume && !bDrawAntennaBeam)
		{
			return;
		}

		const float DebugDrawDist = FMath::Min(ACMAutoLockRange * 0.3f, 300000.0f);

		if (ACMSubMode == ERadarACMSubMode::VerticalScan)
		{
			const float HalfAz = ACMVerticalScanAzimuthWidth * 0.5f;
			auto MakeSwathPoint = [&](float AzDeg, float ElDeg) -> FVector
			{
				const FRotator Rot(ElDeg, AzDeg, 0.0f);
				const FVector LocalDir = Rot.Vector();
				return RadarPosition + RadarRotation.RotateVector(LocalDir) * DebugDrawDist;
			};

			const FVector WorldBL = MakeSwathPoint(-HalfAz, ACMVerticalScanMinElevation);
			const FVector WorldBR = MakeSwathPoint( HalfAz, ACMVerticalScanMinElevation);
			const FVector WorldTL = MakeSwathPoint(-HalfAz, ACMVerticalScanMaxElevation);
			const FVector WorldTR = MakeSwathPoint( HalfAz, ACMVerticalScanMaxElevation);

			const FVector WorldMidMin = MakeSwathPoint(0.0f, ACMVerticalScanMinElevation);
			const FVector WorldMidMax = MakeSwathPoint(0.0f, ACMVerticalScanMaxElevation);

			const FColor SwathColor = FColor::Orange;

			// 4 boundary corner rays from antenna origin
			DrawDebugLine(World, RadarPosition, WorldBL, SwathColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);
			DrawDebugLine(World, RadarPosition, WorldBR, SwathColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);
			DrawDebugLine(World, RadarPosition, WorldTL, SwathColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);
			DrawDebugLine(World, RadarPosition, WorldTR, SwathColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);

			// Perimeter frame at distance
			DrawDebugLine(World, WorldBL, WorldBR, SwathColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);
			DrawDebugLine(World, WorldBR, WorldTR, SwathColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);
			DrawDebugLine(World, WorldTR, WorldTL, SwathColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);
			DrawDebugLine(World, WorldTL, WorldBL, SwathColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);

			// Centerline spine along lift vector
			DrawDebugLine(World, WorldMidMin, WorldMidMax, FColor::Yellow, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.0f);

			// Waterline reference line (0° elevation) if spanned by swath
			if (ACMVerticalScanMinElevation < 0.0f && ACMVerticalScanMaxElevation > 0.0f)
			{
				const FVector WorldML = MakeSwathPoint(-HalfAz, 0.0f);
				const FVector WorldMR = MakeSwathPoint( HalfAz, 0.0f);
				DrawDebugLine(World, WorldML, WorldMR, FColor::Cyan, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.0f);
			}
		}
		else
		{
			FVector AcquisitionDirection = RadarRotation.Vector();
			float ConeHalfAngle = ACMBoresightConeAngle;

			if (ACMSubMode == ERadarACMSubMode::SlewAcquisition)
			{
				const FRotator SlewRotator(ScanCenterElevation, ScanCenterAzimuth, 0.0f);
				AcquisitionDirection = (RadarRotation.Quaternion() * SlewRotator.Quaternion()).GetForwardVector();
				ConeHalfAngle = 10.0f;
			}
			else if (ACMSubMode == ERadarACMSubMode::HelmetCue && !HelmetLookDirection.IsNearlyZero())
			{
				AcquisitionDirection = HelmetLookDirection;
			}

			const float ConeAngleRad = FMath::DegreesToRadians(ConeHalfAngle);
			DrawDebugCone(World, RadarPosition, AcquisitionDirection, DebugDrawDist, ConeAngleRad, ConeAngleRad, 16, FColor::Orange, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f);
		}
		return;
	}

	// 2. Single Target Track (STT) mode visualization: continuous pencil illumination beam
	if (RadarMode == ERadarOperatingMode::SingleTargetTrack)
	{
		if (bDrawAntennaBeam && STTLockedTrackID >= 0)
		{
			const int32 TrackIdx = FindTrackIndex(STTLockedTrackID);
			if (TrackIdx != INDEX_NONE && Tracks[TrackIdx].TrackedActor.IsValid())
			{
				const FVector TargetLoc = Tracks[TrackIdx].TrackedActor->GetActorLocation();
				const FVector BeamDir = (TargetLoc - RadarPosition).GetSafeNormal();
				const float TargetDist = FVector::Dist(RadarPosition, TargetLoc);

				// Draw high-intensity continuous pencil illumination beam to STT target
				DrawDebugLine(World, RadarPosition, TargetLoc, FColor::Red, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 2.5f);
				DrawDebugCone(World, RadarPosition, BeamDir, FMath::Min(TargetDist, 300000.0f), FMath::DegreesToRadians(2.0f), FMath::DegreesToRadians(2.0f), 12, FColor::Red, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.0f);
			}
		}
		return;
	}

	// 3. Spotlight SAR mode visualization: ground stare ray, patch footprint wireframe, and dwell progress
	if (RadarMode == ERadarOperatingMode::Spotlight)
	{
		if (bHasSpotlightPoint)
		{
			FColor StareColor = FColor::Yellow;
			if (bSpotlightGimbalExceeded || bSpotlightInBlindCone)
			{
				StareColor = FColor::Red;
			}
			else if (SpotlightDwellProgress >= 1.0f)
			{
				StareColor = FColor::Green;
			}

			// Stare beam line from aircraft antenna to ground point
			DrawDebugLine(World, RadarPosition, SpotlightTargetLocation, StareColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 2.0f);

			// Ground patch footprint circle on terrain
			DrawDebugCircle(World, SpotlightTargetLocation, SpotlightPatchRadius, 32, StareColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 2.0f, FVector(1, 0, 0), FVector(0, 1, 0), false);

			// Inner dwell synthesis progress circle
			if (SpotlightDwellProgress > 0.01f)
			{
				const float DwellRadius = SpotlightPatchRadius * SpotlightDwellProgress;
				DrawDebugCircle(World, SpotlightTargetLocation, DwellRadius, 24, FColor::Cyan, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.5f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			}

			// Center crosshair marker
			DrawDebugCrosshairs(World, SpotlightTargetLocation, FRotator::ZeroRotator, 150.0f, StareColor, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0);
		}
		return;
	}

	// 4. Search / TWS / GM / SS modes: volume frustum and sweeping antenna cone
	const bool bIsAESA = ScanDrive == ERadarScanDrive::AESA;
	const float VisRange = FMath::Clamp(CurrentDisplayRange * 0.15f, 20000.0f, 300000.0f);
	FVector ReferenceLocation;
	FRotator ReferenceRotation;
	GetRadarReferenceTransform(ReferenceLocation, ReferenceRotation);

	if (bDrawScanVolume && ScanDrive != ERadarScanDrive::SocketDriven)
	{
		FAircraftCombatDebug::DrawRadarFrustum(
			World,
			RadarPosition,
			ReferenceRotation,
			AzimuthScanWidth,
			ElevationScanHeight,
			ScanCenterAzimuth,
			ScanCenterElevation,
			ElevationBars,
			bIsAESA ? -1 : CurrentScanBar,
			VisRange
		);
	}

	if (bDrawAntennaBeam)
	{
		const float ElevationStep = (ElevationBars > 1) ? ElevationScanHeight / static_cast<float>(ElevationBars - 1) : 0.0f;
		const float CurrentBarElevation = ElevationBars > 1
			? ScanCenterElevation - (ElevationScanHeight * 0.5f) + (ElevationStep * CurrentScanBar)
			: ScanCenterElevation;

		FAircraftCombatDebug::DrawAntennaBeam(
			World,
			RadarPosition,
			ScanDrive == ERadarScanDrive::SocketDriven ? RadarRotation : ReferenceRotation,
			ScanDrive == ERadarScanDrive::SocketDriven ? 0.0f : CurrentScanAzimuth,
			ScanDrive == ERadarScanDrive::SocketDriven ? 0.0f : CurrentBarElevation,
			BeamAzimuthWidth * 0.5f,
			BeamElevationWidth * 0.5f,
			VisRange,
			true
		);
	}
}

ERadarIFFResult UAircraftRadarComponent::ClassifyIFF(const AActor* TargetActor) const
{
	if (!bEnableIFF)
	{
		return ERadarIFFResult::Unknown;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !TargetActor)
	{
		return ERadarIFFResult::Unknown;
	}

	// If the target has an IFF transponder and it is turned off (EMCON silent), interrogation produces no response
	if (const UIFFTransponderComponent* Transponder = TargetActor->FindComponentByClass<UIFFTransponderComponent>())
	{
		if (!Transponder->IsTransponderActive())
		{
			return ERadarIFFResult::Unknown;
		}
	}

	const ETeamAttitude::Type FallbackAttitude = FCombatTeamUtility::UnknownAttitudeToTeamAttitude(UnknownContactAttitude);
	const ETeamAttitude::Type Attitude = FCombatTeamUtility::GetAttitude(OwnerActor, TargetActor, FallbackAttitude);
	return FCombatTeamUtility::AttitudeToIFF(Attitude);
}
