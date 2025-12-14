// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
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

FVector2D FRadarBeamSample::GetTargetAngles(const FVector& Position) const
{
	const FRotator Look = PlateRotation.UnrotateVector(Position - Origin).Rotation();
	return FVector2D(Look.Yaw, Look.Pitch);
}

bool FRadarBeamSample::Contains(const FVector& Position) const
{
	const FVector2D Look = GetTargetAngles(Position);
	return FMath::Abs(FMath::FindDeltaAngleDegrees(Angles.X, Look.X)) <= HalfWidths.X + SweepHalfWidths.X &&
		FMath::Abs(Look.Y - Angles.Y) <= HalfWidths.Y + SweepHalfWidths.Y;
}

float FRadarBeamSample::GetGain(const FVector& Position) const
{
	if (!Contains(Position)) return 0.0f;
	const FVector2D Look = GetTargetAngles(Position);
	const float Az = FMath::Max(0.0f, FMath::Abs(FMath::FindDeltaAngleDegrees(Angles.X, Look.X)) - SweepHalfWidths.X) / HalfWidths.X;
	const float El = FMath::Max(0.0f, FMath::Abs(Look.Y - Angles.Y) - SweepHalfWidths.Y) / HalfWidths.Y;
	return FMath::Exp(-0.69314718f * (Az * Az + El * El));
}

FRadarBeamSample UAircraftRadarComponent::MakeBeamSample(float Azimuth, float Elevation, float SweepAzimuth, float SweepElevation) const
{
	FRadarBeamSample Beam;
	GetRadarSourceTransform(Beam.Origin, Beam.PlateRotation);
	Beam.Angles = FVector2D(Azimuth, Elevation);
	Beam.HalfWidths = FVector2D(FMath::Max(0.5f, BeamAzimuthWidth * 0.5f), FMath::Max(0.5f, BeamElevationWidth * 0.5f));
	Beam.SweepHalfWidths = FVector2D(SweepAzimuth, SweepElevation);
	return Beam;
}

void UAircraftRadarComponent::SamplePhysicalPlateBeam(bool bIncludeMotion)
{
	FRadarBeamSample Beam = MakeBeamSample(0.0f, 0.0f);
	const FQuat PlatformRotation = GetOwner() ? GetOwner()->GetActorQuat() : FQuat::Identity;
	if (bIncludeMotion && bHasPreviousPlateSample)
	{
		// Carry the previous plate pose with the aircraft to avoid smearing the beam during aircraft maneuvers.
		const FVector PreviousForward = (PlatformRotation * PreviousPlateRelativeRotation).GetForwardVector();
		const FRotator PreviousLook = Beam.PlateRotation.UnrotateVector(PreviousForward).Rotation();
		Beam.Angles = FVector2D(PreviousLook.Yaw * 0.5f, PreviousLook.Pitch * 0.5f);
		Beam.SweepHalfWidths = FVector2D(FMath::Abs(PreviousLook.Yaw) * 0.5f, FMath::Abs(PreviousLook.Pitch) * 0.5f);
	}
	PreviousPlateRelativeRotation = PlatformRotation.Inverse() * Beam.PlateRotation.Quaternion();
	bHasPreviousPlateSample = true;
	ActiveSampleBeams.Add(Beam);
}

void UAircraftRadarComponent::PerformScanSweep(float DeltaTime)
{
	UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	ActiveSampleBeams.Reset();
	if (ScanDrive == ERadarScanDrive::MSA)
	{
		if (UsesPhysicalPlateBeam()) SamplePhysicalPlateBeam(true);
		else
		{
			bHasPreviousPlateSample = false;
			AdvanceAntennaSweep(DeltaTime);
		}
	}
	else
	{
		bHasPreviousPlateSample = false;
		// Electronic beam scheduling is always relative to the live plate, including its roll.
		const int32 AzCells = FMath::Max(1, FMath::CeilToInt(AzimuthScanWidth / FMath::Max(BeamAzimuthWidth, 1.0f)));
		const int32 Bars = FMath::Clamp(ElevationBars, 1, 8);
		const int32 TotalCells = AzCells * Bars;
		const int32 Visits = ScanDrive == ERadarScanDrive::AESA ? FMath::Clamp(AESABeamsPerSample, 1, 32) : 1;
		for (int32 Visit = 0; Visit < Visits; ++Visit)
		{
			const int32 Cell = ElectronicBeamIndex % TotalCells;
			const int32 AzCell = Cell % AzCells;
			CurrentScanBar = Cell / AzCells;
			CurrentScanAzimuth = ScanCenterAzimuth - AzimuthScanWidth * 0.5f +
				(AzCell + 0.5f) * AzimuthScanWidth / AzCells;
			CurrentScanElevation = ScanCenterElevation - ElevationScanHeight * 0.5f +
				(CurrentScanBar + 0.5f) * ElevationScanHeight / Bars;
			ActiveSampleBeams.Add(MakeBeamSample(CurrentScanAzimuth, CurrentScanElevation));
			ElectronicBeamIndex = (ElectronicBeamIndex + 1) % TotalCells;
			if (ElectronicBeamIndex == 0)
			{
				++ScanSweepCounter;
				OnScanSweepComplete.Broadcast();
			}
		}
		EmitScanProgress(CurrentScanBar,
			FMath::Clamp((CurrentScanAzimuth - ScanCenterAzimuth + AzimuthScanWidth * 0.5f) / FMath::Max(AzimuthScanWidth, 1.0f), 0.0f, 1.0f),
			static_cast<float>(ElectronicBeamIndex) / TotalCells, true);
	}
	const FVector RadarPosition = GetRadarLocation();

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

		const FVector TargetPosition = Candidate->GetActorLocation();

		// Compute candidate local bearing and elevation relative to radar nose
		float TargetBearing = 0.0f;
		float TargetElevation = 0.0f;
		ComputeBearingElevation(TargetPosition, TargetBearing, TargetElevation);

		// Check total scan volume bounds
		const float TargetAzDiff = FMath::Abs(FMath::FindDeltaAngleDegrees(ScanCenterAzimuth, TargetBearing));
		const float TargetElDiff = FMath::Abs(TargetElevation - ScanCenterElevation);
		const bool bInScanVolume = UsesPhysicalPlateBeam() ||
			((AzimuthScanWidth >= 360.0f || TargetAzDiff <= HalfAz) && TargetElDiff <= HalfEl);

		if (!bInScanVolume)
		{
			DebugReject(Candidate, TEXT("Outside scan sector"));
			continue;
		}

		const bool bInSampledBeam = ActiveSampleBeams.ContainsByPredicate(
			[&](const FRadarBeamSample& Beam) { return Beam.Contains(TargetPosition); });
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

		// Update or create track file
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

	// Check actor tags directly
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

	// Reuse cached RCS if track already exists
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

	const float BeamGain = CalculateAntennaBeamGain(TargetPosition);
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
	const APawn* DebugPawn = Cast<APawn>(OwnerActor);
	if (bEnableDebugTraces && bDrawAntennaBeam &&
		(!bDebugOnlyPlayerControlled || !DebugPawn || DebugPawn->IsLocallyControlled()))
	{
		// These are the exact collision-query endpoints and hit, rather than a hypothetical target line.
		const FVector End = bHit && HitResult.bBlockingHit ? HitResult.ImpactPoint : TraceEnd;
		DrawDebugLine(World, TraceStart, End, bHit ? FColor::Red : FColor::Green, false,
			FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds, 0, 1.0f);
		if (bHit) DrawDebugPoint(World, End, 12.0f, FColor::Red, false, FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds);
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

	const FVector LocalDir = RadarRotation.UnrotateVector(ToTarget.GetSafeNormal());
	const FRotator LocalRot = LocalDir.Rotation();

	OutBearing = LocalRot.Yaw;
	OutElevation = LocalRot.Pitch;
}

void UAircraftRadarComponent::DrawDebugScanVolume() const
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !IsRadarEmitting()) return;
	FVector Origin;
	FRotator PlateRotation;
	GetRadarSourceTransform(Origin, PlateRotation);
	const float VisualRange = FMath::Min(MaxDetectionRange, 300000.0f);
	const float Lifetime = FAircraftCombatDebug::RadarDebugDrawLifetimeSeconds;

	if (RadarMode == ERadarOperatingMode::AirCombatManeuver)
	{
		// The acquisition envelope is broader than a physical MSA beam.
		if (bDrawScanVolume || (bDrawAntennaBeam && !UsesPhysicalPlateBeam()))
		{
			const float Range = FMath::Min(ACMAutoLockRange, VisualRange);
			if (ACMSubMode == ERadarACMSubMode::VerticalScan)
			{
				FAircraftCombatDebug::DrawRadarFrustum(World, Origin, PlateRotation,
					ACMVerticalScanAzimuthWidth, ACMVerticalScanMaxElevation - ACMVerticalScanMinElevation,
					0.0f, (ACMVerticalScanMaxElevation + ACMVerticalScanMinElevation) * 0.5f, 0, -1, Range, FColor::Orange);
			}
			else
			{
				const float HalfAngle = FMath::DegreesToRadians(ACMSubMode == ERadarACMSubMode::SlewAcquisition ? 10.0f : ACMBoresightConeAngle);
				DrawDebugCone(World, Origin, GetCommandedBeamDirection(), Range, HalfAngle, HalfAngle,
					16, FColor::Orange, false, Lifetime, 0, 1.5f);
			}
		}
		if (!UsesPhysicalPlateBeam()) return;
	}
	else if (RadarMode == ERadarOperatingMode::Spotlight)
	{
		if (bDrawScanVolume && bHasSpotlightPoint)
		{
			const FColor Color = bSpotlightGimbalExceeded || bSpotlightInBlindCone ? FColor::Red : FColor::Yellow;
			DrawDebugCircle(World, SpotlightTargetLocation, SpotlightPatchRadius, 32, Color, false,
				Lifetime, 0, 2.0f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			DrawDebugCrosshairs(World, SpotlightTargetLocation, FRotator::ZeroRotator, 150.0f, Color, false, Lifetime, 0);
		}
	}
	else if (RadarMode != ERadarOperatingMode::SingleTargetTrack && bDrawScanVolume)
	{
		FAircraftCombatDebug::DrawRadarFrustum(World, Origin, PlateRotation, AzimuthScanWidth,
			ElevationScanHeight, ScanCenterAzimuth, ScanCenterElevation, ElevationBars,
			ScanDrive == ERadarScanDrive::MSA ? CurrentScanBar : -1, VisualRange,
			FColor(0, 220, 100), ScanDrive != ERadarScanDrive::MSA);
	}

	if (!bDrawAntennaBeam) return;
	// Render captured footprints (all AESA visits, including MSA sweep segments), never an invented target ray.
	for (const FRadarBeamSample& Beam : ActiveSampleBeams)
	{
		FAircraftCombatDebug::DrawRadarFrustum(World, Beam.Origin, Beam.PlateRotation,
			2.0f * (Beam.HalfWidths.X + Beam.SweepHalfWidths.X),
			2.0f * (Beam.HalfWidths.Y + Beam.SweepHalfWidths.Y), Beam.Angles.X, Beam.Angles.Y,
			0, -1, VisualRange, FColor::Cyan);
		FAircraftCombatDebug::DrawAntennaBeam(World, Beam.Origin, Beam.PlateRotation,
			Beam.Angles.X, Beam.Angles.Y, Beam.HalfWidths.X, Beam.HalfWidths.Y, VisualRange, false, FColor::Cyan);
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
