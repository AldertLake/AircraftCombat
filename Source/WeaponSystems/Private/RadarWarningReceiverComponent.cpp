// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#include "RadarWarningReceiverComponent.h"
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

URadarWarningReceiverComponent::URadarWarningReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	SetIsReplicatedByDefault(true);
}

void URadarWarningReceiverComponent::BeginPlay()
{
	Super::BeginPlay();
}

void URadarWarningReceiverComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(URadarWarningReceiverComponent, ThreatEntries, COND_OwnerOnly);
}

void URadarWarningReceiverComponent::OnRep_ThreatEntries()
{
	// Detect removed threats
	for (const FRWRThreatEntry& PrevThreat : PreviousThreatEntries)
	{
		const bool bStillPresent = ThreatEntries.ContainsByPredicate([&PrevThreat](const FRWRThreatEntry& Current)
		{
			return Current.ThreatID == PrevThreat.ThreatID;
		});

		if (!bStillPresent)
		{
			OnThreatLost.Broadcast(PrevThreat.ThreatID);
		}
	}

	if (ThreatEntries.IsEmpty() && !PreviousThreatEntries.IsEmpty())
	{
		OnAllThreatsCleared.Broadcast();
	}

	// Detect new or escalated threats
	for (const FRWRThreatEntry& Threat : ThreatEntries)
	{
		const FRWRThreatEntry* PrevThreat = PreviousThreatEntries.FindByPredicate([&Threat](const FRWRThreatEntry& Prev)
		{
			return Prev.ThreatID == Threat.ThreatID;
		});

		if (!PrevThreat)
		{
			OnThreatDetected.Broadcast(Threat);
			if (Threat.ThreatType == ERWRThreatType::MissileLaunch || Threat.ThreatType == ERWRThreatType::MissileSeeker)
			{
				OnMissileLaunchDetected.Broadcast(Threat);
			}
			if (Threat.IsLock() || Threat.IsLaunchWarning() || Threat.bHighestThreatAvailable)
			{
				OnThreatEscalated.Broadcast(Threat);
			}
		}
		else
		{
			if (Threat.ThreatType > PrevThreat->ThreatType)
			{
				OnThreatEscalated.Broadcast(Threat);
				if (Threat.ThreatType == ERWRThreatType::MissileLaunch || Threat.ThreatType == ERWRThreatType::MissileSeeker)
				{
					OnMissileLaunchDetected.Broadcast(Threat);
				}
			}
			else if (Threat.ThreatType < PrevThreat->ThreatType)
			{
				OnThreatDeescalated.Broadcast(Threat);
			}
			else
			{
				OnThreatUpdated.Broadcast(Threat);
			}
		}
	}

	PreviousThreatEntries = ThreatEntries;
}

void URadarWarningReceiverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* OwnerActor = GetOwner();
	const bool bHasAuthority = OwnerActor ? OwnerActor->HasAuthority() : true;

	if (bHasAuthority)
	{
		ScanAccumulator += DeltaTime;

		if (ScanAccumulator >= RWRUpdateInterval)
		{
			// Reset transient escalation/de-escalation flags from previous cycle
			for (FRWRThreatEntry& Threat : ThreatEntries)
			{
				Threat.bIsEscalated = false;
				Threat.bIsDeescalated = false;
			}

			PerformRWRScan();
			ScanForMissileSeekers();
			ScanAccumulator = 0.0f;
		}

		PruneStaleThreats();

		// Designate the single highest-priority (Diamond) threat
		EvaluateHighestThreat();
	}

	// Always update bearings for smooth azimuth strobes on cockpit displays
	UpdateExistingThreats();

	if (bEnableDebugTraces)
	{
		DrawDebugThreats();
	}
}

void URadarWarningReceiverComponent::PerformRWRScan()
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	const FVector RWRPosition = OwnerActor->GetActorLocation();
	const float ScanRange = MaxRWRRange * RWRSensitivity;
	const UAircraftCombatSettings* Settings = UAircraftCombatSettings::Get();

	// Query active radars from subsystem first
	TArray<UAircraftRadarComponent*> RadarCandidates;
	if (UAircraftCombatSubsystem* Subsystem = UAircraftCombatSubsystem::Get(this))
	{
		Subsystem->GetRadarsInRange(RWRPosition, ScanRange, RadarCandidates);
	}

	// Fallback to scene query if no registered radars found in subsystem
	if (RadarCandidates.Num() == 0)
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RWRScan), false, OwnerActor);
		QueryParams.AddIgnoredActor(OwnerActor);

		TArray<FOverlapResult> OverlapResults;
		const FCollisionShape SphereShape = FCollisionShape::MakeSphere(ScanRange);
		if (World->OverlapMultiByChannel(OverlapResults, RWRPosition, FQuat::Identity, RWRDetectionChannel, SphereShape, QueryParams))
		{
			for (const FOverlapResult& Overlap : OverlapResults)
			{
				if (AActor* Src = Overlap.GetActor())
				{
					if (UAircraftRadarComponent* Comp = Src->FindComponentByClass<UAircraftRadarComponent>())
					{
						RadarCandidates.AddUnique(Comp);
					}
				}
			}
		}
	}

	if (RadarCandidates.IsEmpty())
	{
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();
	TSet<AActor*> ProcessedActors;

	for (UAircraftRadarComponent* RadarComp : RadarCandidates)
	{
		if (!RadarComp || !RadarComp->IsRadarEmitting())
		{
			continue;
		}

		AActor* SourceActor = RadarComp->GetOwner();
		if (!SourceActor || SourceActor == OwnerActor || ProcessedActors.Contains(SourceActor))
		{
			continue;
		}
		ProcessedActors.Add(SourceActor);

		if (ShouldIgnoreSource(SourceActor))
		{
			continue;
		}

		const FVector EmitterPos = RadarComp->GetRadarLocation();

		// Line-of-sight terrain masking check: static mountains/hills block RF energy completely
		if (!IsSignalLineOfSightClear(EmitterPos, SourceActor))
		{
			continue;
		}

		// Classify the threat level
		const ERWRThreatType ThreatType = ClassifyRadarThreat(RadarComp);
		if (ThreatType == ERWRThreatType::None)
		{
			continue;
		}

		// Calculate bearing and signal strength
		const float Bearing = ComputeRelativeBearing(EmitterPos);
		const float Range = FVector::Dist(RWRPosition, EmitterPos);
		const float SignalStr = CalculateSignalStrength(Range, ScanRange);

		// Check for existing threat entry (Rule 1A: mutate existing entry)
		const int32 ExistingIdx = FindThreatIndex(SourceActor);

		if (ExistingIdx != INDEX_NONE)
		{
			// Update existing threat
			FRWRThreatEntry& Existing = ThreatEntries[ExistingIdx];
			const ERWRThreatType OldType = Existing.ThreatType;

			Existing.ThreatType = ThreatType;
			Existing.BearingDegrees = Bearing;
			Existing.SignalStrength = SignalStr;
			Existing.Range = Range;
			Existing.TimeLastUpdated = CurrentTime;
			Existing.LastSignalTime = CurrentTime;
			Existing.bIsNewThreat = false;

			if (ThreatType >= ERWRThreatType::LockOnRadar)
			{
				Existing.LastLockTime = CurrentTime;
			}

			if (Existing.EmitterType == NAME_None)
			{
				Existing.EmitterType = ResolveEmitterType(SourceActor);
			}

			if (Settings)
			{
				Existing.VehicleType = Settings->ResolveTargetDomain(SourceActor);
			}

			// Check for escalation or de-escalation
			if (static_cast<uint8>(ThreatType) > static_cast<uint8>(OldType))
			{
				Existing.bIsEscalated = true;
				Existing.bIsDeescalated = false;
				OnThreatEscalated.Broadcast(Existing);
				if (ThreatType == ERWRThreatType::MissileLaunch && OldType != ERWRThreatType::MissileLaunch)
				{
					OnMissileLaunchDetected.Broadcast(Existing);
				}
			}
			else if (static_cast<uint8>(ThreatType) < static_cast<uint8>(OldType))
			{
				Existing.bIsDeescalated = true;
				Existing.bIsEscalated = false;
				OnThreatDeescalated.Broadcast(Existing);
			}

			OnThreatUpdated.Broadcast(Existing);
		}
		else
		{
			// Create new threat entry
			FRWRThreatEntry NewThreat;
			NewThreat.ThreatID = NextThreatID++;
			NewThreat.SourceActor = SourceActor;
			NewThreat.ThreatType = ThreatType;
			NewThreat.VehicleType = Settings ? Settings->ResolveTargetDomain(SourceActor) : ERadarTargetDomain::Air;
			NewThreat.BearingDegrees = Bearing;
			NewThreat.SignalStrength = SignalStr;
			NewThreat.Range = Range;
			NewThreat.TimeDetected = CurrentTime;
			NewThreat.TimeLastUpdated = CurrentTime;
			NewThreat.LastSignalTime = CurrentTime;
			NewThreat.LastLockTime = (ThreatType >= ERWRThreatType::LockOnRadar) ? CurrentTime : 0.0f;
			NewThreat.bIsActiveMissile = false;
			NewThreat.bIsNewThreat = true;

			// Identify emitter type from EmitterType=X tag (e.g. "EmitterType=F-16", "EmitterType=SA-10")
			NewThreat.EmitterType = ResolveEmitterType(SourceActor);

			ThreatEntries.Add(NewThreat);
			OnThreatDetected.Broadcast(NewThreat);

			if (NewThreat.IsLock() || NewThreat.IsLaunchWarning())
			{
				NewThreat.bIsEscalated = true;
				OnThreatEscalated.Broadcast(NewThreat);
				if (ThreatType == ERWRThreatType::MissileLaunch)
				{
					OnMissileLaunchDetected.Broadcast(NewThreat);
				}
			}
		}
	}
}

ERWRThreatType URadarWarningReceiverComponent::ClassifyRadarThreat(const UAircraftRadarComponent* RadarComp) const
{
	if (!RadarComp || !RadarComp->IsRadarEmitting())
	{
		return ERWRThreatType::None;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return ERWRThreatType::None;
	}

	// IFF: Classify friendly emitters as FriendlyRadar (when IFF is enabled but not hiding)
	if (bEnableIFF && !bHideFriendlyEmitters)
	{
		const AActor* RadarOwner = RadarComp->GetOwner();
		if (RadarOwner && FCombatTeamUtility::IsFriendly(OwnerActor, RadarOwner))
		{
			return ERWRThreatType::FriendlyRadar;
		}
	}

	// If the radar has terrain masking enabled, check if static terrain blocks the RF signal from reaching this aircraft
	if (RadarComp->bEnableTerrainMasking)
	{
		const AActor* RadarOwner = RadarComp->GetOwner();
		if (RadarOwner && RadarComp->IsTerrainMasked(RadarComp->GetRadarLocation(), OwnerActor->GetActorLocation(), OwnerActor))
		{
			return ERWRThreatType::None;
		}
	}

	// An actual launched SARH/hybrid missile raises the support radar's threat level.
	const bool bGuidingLaunch = RadarComp->GetActiveGuidingMissiles().ContainsByPredicate(
		[OwnerActor](const URadarMissileGuidanceComponent* Missile)
		{
			return IsValid(Missile) && Missile->IsWeaponFired() &&
				Missile->GetLockedTarget() == OwnerActor && Missile->HasVerifiedIllumination();
		});
	if (bGuidingLaunch) return ERWRThreatType::MissileLaunch;

	// Check if we are the STT locked target (highest non-missile threat)
	if (RadarComp->IsSTTLocked())
	{
		const AActor* LockedActor = RadarComp->GetSTTLockedActor();
		if (LockedActor == OwnerActor && RadarComp->IsContinuousWaveIlluminating(OwnerActor))
		{
			return ERWRThreatType::LockOnRadar;
		}
	}

	// Check if we are in the radar's TWS track list
	if (IsInRadarTrackList(RadarComp))
	{
		return ERWRThreatType::TrackingRadar;
	}

	// Check if we are in the radar's scan volume (being illuminated) or in close sidelobe range
	const ERadarOperatingMode Mode = RadarComp->GetRadarMode();
	if (Mode == ERadarOperatingMode::Search || Mode == ERadarOperatingMode::TrackWhileScan ||
	    Mode == ERadarOperatingMode::AirCombatManeuver)
	{
		const AActor* RadarOwner = RadarComp->GetOwner();
		const float Dist = RadarOwner ? FVector::Dist(OwnerActor->GetActorLocation(), RadarComp->GetRadarLocation()) : 0.0f;
		if (Dist < 500000.0f || RadarComp->IsTargetInScanVolume(const_cast<AActor*>(OwnerActor)))
		{
			return ERWRThreatType::SearchRadar;
		}
	}

	return ERWRThreatType::None;
}

bool URadarWarningReceiverComponent::IsInRadarTrackList(const UAircraftRadarComponent* RadarComp) const
{
	if (!RadarComp)
	{
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	const TArray<FRadarTrack>& Tracks = RadarComp->GetAllTracks();

	for (const FRadarTrack& Track : Tracks)
	{
		if (Track.TrackedActor.IsValid() && Track.TrackedActor.Get() == OwnerActor)
		{
			return true;
		}
	}

	return false;
}

void URadarWarningReceiverComponent::ScanForMissileSeekers()
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	UAircraftCombatSubsystem* CombatSubsystem = World->GetSubsystem<UAircraftCombatSubsystem>();
	if (!CombatSubsystem)
	{
		return;
	}

	const FVector RWRPosition = OwnerActor->GetActorLocation();
	const float CurrentTime = World->GetTimeSeconds();
	const float MissileScanRange = MaxRWRRange * 0.5f * RWRSensitivity;

	TArray<URadarMissileGuidanceComponent*> MissileCandidates;
	CombatSubsystem->GetActiveMissileSeekersInRange(RWRPosition, MissileScanRange, MissileCandidates);

	TSet<AActor*> ProcessedActors;
	for (URadarMissileGuidanceComponent* MissileComp : MissileCandidates)
	{
		if (!MissileComp)
		{
			continue;
		}

		// RULE 1 & 2: RWR detects EMITTERS, not physical objects!
		// Only Active Radar Missiles (Fox 3 / AMRAAM) actively emitting in Terminal/Pitbull phase radiate RF waves!
		// SARH (AIM-7), IR (AIM-9), ARM (HARM), and mid-course ARH emit zero RF and are 100% skipped!
		if (!MissileComp->IsSeekerEmittingRF())
		{
			continue;
		}

		AActor* MissileActor = MissileComp->GetOwner();
		if (!MissileActor || MissileActor == OwnerActor || ProcessedActors.Contains(MissileActor))
		{
			continue;
		}
		ProcessedActors.Add(MissileActor);

		if (ShouldIgnoreSource(MissileActor))
		{
			continue;
		}

		const FVector MissilePos = MissileActor->GetActorLocation();
		const float Range = FVector::Dist(RWRPosition, MissilePos);

		// Effective seeker transmitter range
		const float SeekerMaxRange = MissileComp->ActiveSeekerMaxRange > 0.0f
			? MissileComp->ActiveSeekerMaxRange
			: (MissileComp->ActiveSeekerRange > 0.0f ? MissileComp->ActiveSeekerRange * 1.5f : 2000000.0f);

		if (Range > SeekerMaxRange)
		{
			continue;
		}

		// Check if the missile's seeker beam is illuminating our aircraft
		const bool bIsTargetingUs = (MissileComp->GetLockedTarget() == OwnerActor);
		if (!bIsTargetingUs)
		{
			// If not locked directly, evaluate whether our aircraft falls within the active seeker transmitting cone
			FVector SeekerLoc;
			FRotator SeekerRot;
			MissileComp->GetSeekerTransform(SeekerLoc, SeekerRot);
			const FVector ToRWR = (RWRPosition - SeekerLoc).GetSafeNormal();
			const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(SeekerRot.Vector(), ToRWR), -1.0f, 1.0f)));
			if (AngleDeg > MissileComp->ActiveSeekerConeAngle * 1.5f)
			{
				continue; // Not illuminated by missile seeker transmission
			}
		}

		// Terrain line-of-sight check: static terrain/hills completely mask RF emissions
		if (!IsSignalLineOfSightClear(MissilePos, MissileActor))
		{
			continue;
		}

		const float Bearing = ComputeRelativeBearing(MissilePos);
		const float SignalStr = CalculateSignalStrength(Range, SeekerMaxRange);

		const int32 ExistingIdx = FindThreatIndex(MissileActor);

		if (ExistingIdx != INDEX_NONE)
		{
			FRWRThreatEntry& Existing = ThreatEntries[ExistingIdx];
			Existing.ThreatType = ERWRThreatType::MissileSeeker;
			Existing.VehicleType = ERadarTargetDomain::Missile;
			Existing.BearingDegrees = Bearing;
			Existing.SignalStrength = SignalStr;
			Existing.Range = Range;
			Existing.TimeLastUpdated = CurrentTime;
			Existing.LastSignalTime = CurrentTime;
			Existing.LastLockTime = CurrentTime;
			Existing.bIsActiveMissile = true;

			if (Existing.EmitterType == NAME_None)
			{
				Existing.EmitterType = ResolveEmitterType(MissileActor);
			}

			OnThreatUpdated.Broadcast(Existing);
		}
		else
		{
			// Create a separate launch threat entry for active radar missiles (Pitbull)
			// The launching aircraft's track remains on display alongside the missile
			FRWRThreatEntry NewThreat;
			NewThreat.ThreatID = NextThreatID++;
			NewThreat.SourceActor = MissileActor;
			NewThreat.ThreatType = ERWRThreatType::MissileSeeker;
			NewThreat.VehicleType = ERadarTargetDomain::Missile;
			NewThreat.BearingDegrees = Bearing;
			NewThreat.SignalStrength = SignalStr;
			NewThreat.Range = Range;
			NewThreat.TimeDetected = CurrentTime;
			NewThreat.TimeLastUpdated = CurrentTime;
			NewThreat.LastSignalTime = CurrentTime;
			NewThreat.LastLockTime = CurrentTime;
			NewThreat.bIsActiveMissile = true;
			NewThreat.bIsNewThreat = true;
			NewThreat.bIsEscalated = true;
			NewThreat.EmitterType = ResolveEmitterType(MissileActor);

			ThreatEntries.Add(NewThreat);
			OnThreatDetected.Broadcast(NewThreat);
			OnMissileLaunchDetected.Broadcast(NewThreat);
			OnThreatEscalated.Broadcast(NewThreat);
		}
	}
}

void URadarWarningReceiverComponent::UpdateExistingThreats()
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	const float ScanRange = FMath::Max(100.0f, MaxRWRRange * RWRSensitivity);

	for (FRWRThreatEntry& Threat : ThreatEntries)
	{
		if (Threat.SourceActor.IsValid())
		{
			const FVector EmitterPos = Threat.SourceActor->GetActorLocation();
			Threat.BearingDegrees = ComputeRelativeBearing(EmitterPos);
			Threat.Range = FVector::Dist(OwnerActor->GetActorLocation(), EmitterPos);

			float ReferenceRange = ScanRange;
			if (Threat.ThreatType == ERWRThreatType::MissileSeeker || Threat.ThreatType == ERWRThreatType::MissileLaunch)
			{
				if (const URadarMissileGuidanceComponent* MissileComp = Threat.SourceActor->FindComponentByClass<URadarMissileGuidanceComponent>())
				{
					ReferenceRange = FMath::Max(100.0f, (MissileComp->ActiveSeekerRange > 0.0f ? MissileComp->ActiveSeekerRange * 1.5f : ScanRange * 0.5f));
				}
				else
				{
					ReferenceRange = ScanRange * 0.5f;
				}
			}

			Threat.SignalStrength = CalculateSignalStrength(Threat.Range, ReferenceRange);
		}
		Threat.bIsNewThreat = false;
	}
}

void URadarWarningReceiverComponent::PruneStaleThreats()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();
	bool bHadThreats = ThreatEntries.Num() > 0;

	for (int32 i = ThreatEntries.Num() - 1; i >= 0; --i)
	{
		FRWRThreatEntry& Threat = ThreatEntries[i];
		bool bShouldRemove = false;

		// 1. Source actor destroyed or invalid
		if (!Threat.SourceActor.IsValid())
		{
			bShouldRemove = true;
		}
		// 2. Autonomous active missile seekers (Pitbull)
		else if (Threat.bIsActiveMissile || Threat.ThreatType == ERWRThreatType::MissileSeeker)
		{
			if ((CurrentTime - Threat.LastSignalTime) > MissileSeekerTimeoutSeconds)
			{
				bShouldRemove = true;
			}
		}
		// 3. Radar platforms (Search, TWS, STT, Launch)
		else
		{
			// Drop from Launch/Lock to Search if continuous RF illumination is lost
			if (Threat.ThreatType == ERWRThreatType::LockOnRadar || Threat.ThreatType == ERWRThreatType::MissileLaunch)
			{
				if ((CurrentTime - Threat.LastLockTime) >= LockLossGracePeriod)
				{
					// RF illumination lost; de-escalate back to search
					Threat.ThreatType = ERWRThreatType::SearchRadar;
					Threat.bIsDeescalated = true;
					Threat.bIsEscalated = false;
					OnThreatDeescalated.Broadcast(Threat);
					OnThreatUpdated.Broadcast(Threat);
				}
			}

			// Prune search tracks after sweep timeout
			const float MaxSearchTimeout = FMath::Max(SearchThreatTimeoutSeconds, ThreatTimeoutSeconds);
			if ((CurrentTime - Threat.LastSignalTime) > MaxSearchTimeout)
			{
				bShouldRemove = true;
			}
		}

		if (bShouldRemove)
		{
			const int32 LostThreatID = Threat.ThreatID;
			ThreatEntries.RemoveAt(i);
			OnThreatLost.Broadcast(LostThreatID);
		}
	}

	if (bHadThreats && ThreatEntries.Num() == 0)
	{
		OnAllThreatsCleared.Broadcast();
	}
}

float URadarWarningReceiverComponent::ComputeRelativeBearing(const FVector& SourcePosition) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return 0.0f;
	}

	const FVector ToSource = SourcePosition - OwnerActor->GetActorLocation();
	const FVector LocalDir = OwnerActor->GetActorRotation().UnrotateVector(ToSource.GetSafeNormal());
	const FRotator LocalRot = LocalDir.Rotation();

	return LocalRot.Yaw; // -180 to 180, 0 = nose
}

int32 URadarWarningReceiverComponent::FindThreatIndex(const AActor* SourceActor) const
{
	for (int32 i = 0; i < ThreatEntries.Num(); ++i)
	{
		if (ThreatEntries[i].SourceActor.IsValid() && ThreatEntries[i].SourceActor.Get() == SourceActor)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool URadarWarningReceiverComponent::ShouldIgnoreSource(const AActor* SourceActor) const
{
	if (!IsValid(SourceActor))
	{
		return true;
	}

	// Filter friendly emitters from the RWR display
	if (bEnableIFF && bHideFriendlyEmitters)
	{
		if (FCombatTeamUtility::IsFriendly(GetOwner(), SourceActor))
		{
			return true;
		}
	}

	// Filter emitters matching ignore tags
	for (const FName& IgnoreTag : RWRIgnoreTags)
	{
		if (SourceActor->ActorHasTag(IgnoreTag))
		{
			return true;
		}
	}

	return false;
}

void URadarWarningReceiverComponent::GetThreatsByType(ERWRThreatType InType, TArray<FRWRThreatEntry>& OutThreats) const
{
	OutThreats.Reset();
	for (const FRWRThreatEntry& Threat : ThreatEntries)
	{
		if (Threat.ThreatType == InType)
		{
			OutThreats.Add(Threat);
		}
	}
}

bool URadarWarningReceiverComponent::GetHighestThreat(FRWRThreatEntry& OutThreat) const
{
	if (ThreatEntries.Num() == 0)
	{
		return false;
	}

	// Check for designated diamond threat first
	for (const FRWRThreatEntry& Threat : ThreatEntries)
	{
		if (Threat.bHighestThreatAvailable)
		{
			OutThreat = Threat;
			return true;
		}
	}

	// Fallback to highest threat level
	int32 HighestIdx = 0;
	uint8 HighestLevel = static_cast<uint8>(ThreatEntries[0].ThreatType);

	for (int32 i = 1; i < ThreatEntries.Num(); ++i)
	{
		const uint8 Level = static_cast<uint8>(ThreatEntries[i].ThreatType);
		if (Level > HighestLevel)
		{
			HighestLevel = Level;
			HighestIdx = i;
		}
	}

	OutThreat = ThreatEntries[HighestIdx];
	return true;
}

bool URadarWarningReceiverComponent::HasActiveLockOnThreat() const
{
	for (const FRWRThreatEntry& Threat : ThreatEntries)
	{
		if (Threat.ThreatType >= ERWRThreatType::LockOnRadar)
		{
			return true;
		}
	}
	return false;
}

bool URadarWarningReceiverComponent::HasMissileLaunchWarning() const
{
	for (const FRWRThreatEntry& Threat : ThreatEntries)
	{
		if (Threat.ThreatType == ERWRThreatType::MissileLaunch || Threat.ThreatType == ERWRThreatType::MissileSeeker)
		{
			return true;
		}
	}
	return false;
}

float URadarWarningReceiverComponent::GetBearingToThreat(int32 ThreatID) const
{
	for (const FRWRThreatEntry& Threat : ThreatEntries)
	{
		if (Threat.ThreatID == ThreatID)
		{
			return Threat.BearingDegrees;
		}
	}
	return 0.0f;
}

ERWRThreatType URadarWarningReceiverComponent::GetHighestThreatLevel() const
{
	ERWRThreatType Highest = ERWRThreatType::None;
	for (const FRWRThreatEntry& Threat : ThreatEntries)
	{
		if (static_cast<uint8>(Threat.ThreatType) > static_cast<uint8>(Highest))
		{
			Highest = Threat.ThreatType;
		}
	}
	return Highest;
}

void URadarWarningReceiverComponent::DrawDebugThreats() const
{
	UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	const FVector OwnerPos = OwnerActor->GetActorLocation();

	FAircraftCombatDebug::DrawRWRThreatStrobes(World, this, OwnerPos, ThreatEntries, bDrawThreatStrobes);

	if (bEnableDiagnosticHUD)
	{
		FAircraftCombatDebug::PrintRWRTelemetry(ThreatEntries.Num(), HasActiveLockOnThreat(), HasMissileLaunchWarning());
	}
}

FName URadarWarningReceiverComponent::ResolveEmitterType(const AActor* SourceActor, FName DefaultFallback) const
{
	if (!IsValid(SourceActor))
	{
		return DefaultFallback;
	}

	auto TryParseEmitterTag = [](const FName& TagName, FName& OutType) -> bool
	{
		const FString TagStr = TagName.ToString();
		if (TagStr.StartsWith(TEXT("EmitterType="), ESearchCase::IgnoreCase) || TagStr.StartsWith(TEXT("EmitterType:"), ESearchCase::IgnoreCase))
		{
			const FString ValueStr = TagStr.RightChop(12).TrimStartAndEnd();
			if (!ValueStr.IsEmpty())
			{
				OutType = FName(*ValueStr);
				return true;
			}
		}
		else if (TagStr.StartsWith(TEXT("Emitter="), ESearchCase::IgnoreCase) || TagStr.StartsWith(TEXT("Emitter:"), ESearchCase::IgnoreCase))
		{
			const FString ValueStr = TagStr.RightChop(8).TrimStartAndEnd();
			if (!ValueStr.IsEmpty())
			{
				OutType = FName(*ValueStr);
				return true;
			}
		}
		return false;
	};

	// 1. Check Source Actor tags
	for (const FName& Tag : SourceActor->Tags)
	{
		FName ParsedType;
		if (TryParseEmitterTag(Tag, ParsedType))
		{
			return ParsedType;
		}
	}

	// 2. Check Component tags on the source actor (e.g. radar or missile component)
	TInlineComponentArray<UActorComponent*> Components(SourceActor);
	for (const UActorComponent* Comp : Components)
	{
		if (Comp)
		{
			for (const FName& CompTag : Comp->ComponentTags)
			{
				FName ParsedType;
				if (TryParseEmitterTag(CompTag, ParsedType))
				{
					return ParsedType;
				}
			}
		}
	}

	return DefaultFallback;
}

float URadarWarningReceiverComponent::CalculateSignalStrength(float Range, float MaxRange)
{
	if (MaxRange <= 0.0f)
	{
		return 0.0f;
	}

	const float NormRange = FMath::Clamp(Range / MaxRange, 0.0f, 1.0f);
	return FMath::Square(1.0f - NormRange);
}

bool URadarWarningReceiverComponent::IsSignalLineOfSightClear(const FVector& EmitterLocation, const AActor* EmitterActor) const
{
	if (!bEnableTerrainMasking)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return true;
	}

	const FVector RWRLocation = OwnerActor->GetActorLocation();

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RWRLineOfSight), true);
	TraceParams.AddIgnoredActor(OwnerActor);

	TArray<AActor*> OwnerAttached;
	OwnerActor->GetAttachedActors(OwnerAttached, true, true);
	TraceParams.AddIgnoredActors(OwnerAttached);

	if (EmitterActor)
	{
		TraceParams.AddIgnoredActor(EmitterActor);
		TArray<AActor*> EmitterAttached;
		EmitterActor->GetAttachedActors(EmitterAttached, true, true);
		TraceParams.AddIgnoredActors(EmitterAttached);
	}

	FHitResult HitResult;
	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		EmitterLocation,
		RWRLocation,
		LineOfSightChannel,
		TraceParams
	);

	if (!bHit)
	{
		return true;
	}

	// If hit is owner actor or attached component, LOS is clear
	if (HitResult.GetActor() == OwnerActor || OwnerAttached.Contains(HitResult.GetActor()))
	{
		return true;
	}

	// Hit static world geometry or terrain blocking the RF beam
	return false;
}

void URadarWarningReceiverComponent::EvaluateHighestThreat()
{
	// 1. Reset bHighestThreatAvailable across all threats
	for (FRWRThreatEntry& Threat : ThreatEntries)
	{
		Threat.bHighestThreatAvailable = false;
	}

	// 2. If feature is disabled or no threats exist, leave all false
	if (!bFindHighestThreatAvailable || ThreatEntries.IsEmpty())
	{
		return;
	}

	const UAircraftCombatSettings* Settings = UAircraftCombatSettings::Get();
	if (!Settings || Settings->ThreatTypePriority.IsEmpty())
	{
		return;
	}

	int32 BestIndex = INDEX_NONE;
	int32 BestTypeRank = MAX_int32;
	int32 BestDomainRank = MAX_int32;
	float BestRange = MAX_flt;
	float BestSignalStrength = -1.0f;

	for (int32 i = 0; i < ThreatEntries.Num(); ++i)
	{
		const FRWRThreatEntry& Candidate = ThreatEntries[i];

		// Check primary: ThreatType in ThreatTypePriority (Index 0 = most dangerous)
		const int32 TypeRank = Settings->ThreatTypePriority.Find(Candidate.ThreatType);
		if (TypeRank == INDEX_NONE)
		{
			// Threat type not ranked as dangerous enough to receive Diamond Threat
			continue;
		}

		// Check secondary: VehicleType in ThreatVehicleTypePriority (Index 0 = most dangerous)
		int32 DomainRank = Settings->ThreatVehicleTypePriority.Find(Candidate.VehicleType);
		if (DomainRank == INDEX_NONE)
		{
			DomainRank = MAX_int32;
		}

		bool bIsBetter = false;

		if (BestIndex == INDEX_NONE)
		{
			bIsBetter = true;
		}
		else if (TypeRank < BestTypeRank)
		{
			bIsBetter = true;
		}
		else if (TypeRank == BestTypeRank)
		{
			if (DomainRank < BestDomainRank)
			{
				bIsBetter = true;
			}
			else if (DomainRank == BestDomainRank)
			{
				// Closer range is more dangerous; if ranges equal, higher signal strength
				if (Candidate.Range > 0.0f && BestRange > 0.0f)
				{
					if (Candidate.Range < BestRange)
					{
						bIsBetter = true;
					}
				}
				else if (Candidate.SignalStrength > BestSignalStrength)
				{
					bIsBetter = true;
				}
			}
		}

		if (bIsBetter)
		{
			BestIndex = i;
			BestTypeRank = TypeRank;
			BestDomainRank = DomainRank;
			BestRange = Candidate.Range;
			BestSignalStrength = Candidate.SignalStrength;
		}
	}

	// 3. Mark the single best candidate as highest threat
	if (BestIndex != INDEX_NONE && ThreatEntries.IsValidIndex(BestIndex))
	{
		ThreatEntries[BestIndex].bHighestThreatAvailable = true;
	}
}
