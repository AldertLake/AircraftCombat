// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
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

void URadarWarningReceiverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ScanAccumulator += DeltaTime;

	if (ScanAccumulator >= RWRUpdateInterval)
	{
		PerformRWRScan();
		ScanForMissileSeekers();
		ScanAccumulator = 0.0f;
	}

	// Always update bearings and prune stale threats
	UpdateExistingThreats();
	PruneStaleThreats();

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

		// Classify the threat level
		const ERWRThreatType ThreatType = ClassifyRadarThreat(RadarComp);
		if (ThreatType == ERWRThreatType::None)
		{
			continue;
		}

		// Calculate bearing and signal strength
		const float Bearing = ComputeRelativeBearing(SourceActor->GetActorLocation());
		const float Range = FVector::Dist(RWRPosition, SourceActor->GetActorLocation());
		const float NormRange = FMath::Clamp(Range / ScanRange, 0.0f, 1.0f);
		const float SignalStr = FMath::Clamp(1.0f - (NormRange * NormRange), 0.0f, 1.0f);

		// Check for existing threat entry
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
			Existing.bIsNewThreat = false;
			Existing.bIsCritical = (ThreatType >= ERWRThreatType::LockOnRadar);

			// Check for escalation or de-escalation
			if (static_cast<uint8>(ThreatType) > static_cast<uint8>(OldType))
			{
				OnThreatEscalated.Broadcast(Existing);
			}
			else if (static_cast<uint8>(ThreatType) < static_cast<uint8>(OldType))
			{
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
			NewThreat.BearingDegrees = Bearing;
			NewThreat.SignalStrength = SignalStr;
			NewThreat.Range = Range;
			NewThreat.TimeDetected = CurrentTime;
			NewThreat.TimeLastUpdated = CurrentTime;
			NewThreat.bIsNewThreat = true;
			NewThreat.bIsCritical = (ThreatType >= ERWRThreatType::LockOnRadar);

			// Try to identify emitter type from actor tags
			for (const FName& Tag : SourceActor->Tags)
			{
				if (Tag != NAME_None)
				{
					NewThreat.EmitterType = Tag;
					break;
				}
			}

			ThreatEntries.Add(NewThreat);
			OnThreatDetected.Broadcast(NewThreat);

			if (NewThreat.bIsCritical)
			{
				OnThreatEscalated.Broadcast(NewThreat);
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

	// If the radar has terrain masking enabled, check if static terrain blocks the RF signal from reaching this aircraft
	if (RadarComp->bEnableTerrainMasking)
	{
		const AActor* RadarOwner = RadarComp->GetOwner();
		if (RadarOwner && RadarComp->IsTerrainMasked(RadarOwner->GetActorLocation(), OwnerActor->GetActorLocation(), OwnerActor))
		{
			return ERWRThreatType::None;
		}
	}

	// Check if we are the STT locked target (highest non-missile threat)
	if (RadarComp->IsSTTLocked())
	{
		const AActor* LockedActor = RadarComp->GetSTTLockedActor();
		if (LockedActor == OwnerActor)
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
		const float Dist = RadarOwner ? FVector::Dist(OwnerActor->GetActorLocation(), RadarOwner->GetActorLocation()) : 0.0f;
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

		// Check if the missile's seeker is active and targeting us
		const bool bIsTargetingUs = (MissileComp->GetLockedTarget() == OwnerActor);
		const bool bIsMissileActive = MissileComp->IsWeaponActivated();
		const bool bIsTerminal = MissileComp->IsInTerminalPhase();

		if (!bIsMissileActive)
		{
			continue;
		}

		ERWRThreatType MissileThreatType = ERWRThreatType::None;

		if (bIsTargetingUs && bIsTerminal)
		{
			MissileThreatType = ERWRThreatType::MissileSeeker;
		}
		else if (bIsTargetingUs)
		{
			MissileThreatType = ERWRThreatType::MissileLaunch;
		}

		if (MissileThreatType == ERWRThreatType::None)
		{
			continue;
		}

		const float Bearing = ComputeRelativeBearing(MissileActor->GetActorLocation());
		const float Range = FVector::Dist(RWRPosition, MissileActor->GetActorLocation());
		const float SignalStr = FMath::Clamp(1.0f - FMath::Square(Range / MissileScanRange), 0.0f, 1.0f);

		const int32 ExistingIdx = FindThreatIndex(MissileActor);

		if (ExistingIdx != INDEX_NONE)
		{
			FRWRThreatEntry& Existing = ThreatEntries[ExistingIdx];
			const ERWRThreatType OldType = Existing.ThreatType;

			Existing.ThreatType = MissileThreatType;
			Existing.BearingDegrees = Bearing;
			Existing.SignalStrength = SignalStr;
			Existing.Range = Range;
			Existing.TimeLastUpdated = CurrentTime;
			Existing.bIsCritical = true;

			if (static_cast<uint8>(MissileThreatType) > static_cast<uint8>(OldType))
			{
				OnThreatEscalated.Broadcast(Existing);
			}
			else if (static_cast<uint8>(MissileThreatType) < static_cast<uint8>(OldType))
			{
				OnThreatDeescalated.Broadcast(Existing);
			}
			OnThreatUpdated.Broadcast(Existing);
		}
		else
		{
			FRWRThreatEntry NewThreat;
			NewThreat.ThreatID = NextThreatID++;
			NewThreat.SourceActor = MissileActor;
			NewThreat.ThreatType = MissileThreatType;
			NewThreat.BearingDegrees = Bearing;
			NewThreat.SignalStrength = SignalStr;
			NewThreat.Range = Range;
			NewThreat.TimeDetected = CurrentTime;
			NewThreat.TimeLastUpdated = CurrentTime;
			NewThreat.bIsNewThreat = true;
			NewThreat.bIsCritical = true;

			ThreatEntries.Add(NewThreat);
			OnThreatDetected.Broadcast(NewThreat);

			if (MissileThreatType == ERWRThreatType::MissileLaunch)
			{
				OnMissileLaunchDetected.Broadcast(NewThreat);
			}
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

	for (FRWRThreatEntry& Threat : ThreatEntries)
	{
		if (Threat.SourceActor.IsValid())
		{
			Threat.BearingDegrees = ComputeRelativeBearing(Threat.SourceActor->GetActorLocation());
			Threat.Range = FVector::Dist(OwnerActor->GetActorLocation(), Threat.SourceActor->GetActorLocation());
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
		const FRWRThreatEntry& Threat = ThreatEntries[i];

		bool bShouldRemove = false;

		// Check if source actor is still valid
		if (!Threat.SourceActor.IsValid())
		{
			bShouldRemove = true;
		}
		// Check for timeout
		else if ((CurrentTime - Threat.TimeLastUpdated) > ThreatTimeoutSeconds)
		{
			bShouldRemove = true;
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

	FAircraftCombatDebug::DrawRWRThreatStrobes(World, OwnerPos, ThreatEntries, bDrawThreatStrobes);

	if (bEnableDiagnosticHUD)
	{
		FAircraftCombatDebug::PrintRWRTelemetry(ThreatEntries.Num(), HasActiveLockOnThreat(), HasMissileLaunchWarning());
	}
}
