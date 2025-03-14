// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "RadarMissileGuidanceComponent.h"
#include "AircraftRadarComponent.h"
#include "AircraftCombatSettings.h"
#include "AircraftCombatSubsystem.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"

URadarMissileGuidanceComponent::URadarMissileGuidanceComponent()
{
	TargetTrackingSocket = NAME_None;
	SetIsReplicatedByDefault(true);
}

void URadarMissileGuidanceComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UAircraftCombatSubsystem* Registry = UAircraftCombatSubsystem::Get(this))
		Registry->RegisterMissileSeeker(this);
}

void URadarMissileGuidanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAircraftRadarComponent* Radar = LaunchRadar.Get())
		Radar->UnregisterLaunchedRadarMissile(this);
	if (UAircraftRadarComponent* Radar = Illuminator.Get())
		Radar->UnregisterGuidingMissile(this);
	if (UAircraftCombatSubsystem* Registry = UAircraftCombatSubsystem::Get(this))
		Registry->UnregisterMissileSeeker(this);
	Super::EndPlay(EndPlayReason);
}

void URadarMissileGuidanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URadarMissileGuidanceComponent, FlightPhase);
	DOREPLIFETIME(URadarMissileGuidanceComponent, bSeekerTransmitting);
}

bool URadarMissileGuidanceComponent::PrepareLaunch(const FMissileLaunchConfiguration& Configuration)
{
	UAircraftRadarComponent* PreviousUplink = Uplink.Get();
	FMissileLaunchConfiguration CoreConfiguration = Configuration;
	if (!bNeedsIllumination) CoreConfiguration.Target.TargetActor = nullptr;
	if (!Super::PrepareLaunch(CoreConfiguration)) return false;
	TargetSolution = Configuration.Target;
	Illuminator = Configuration.Illuminator;
	Uplink = Configuration.Uplink;
	UAircraftRadarComponent* ResolvedLauncher = Configuration.LaunchRadar;
	if (!IsValid(ResolvedLauncher) && IsValid(Configuration.Carrier))
		ResolvedLauncher = Configuration.Carrier->FindComponentByClass<UAircraftRadarComponent>();
	if (!IsValid(ResolvedLauncher) && IsValid(Configuration.Uplink)) ResolvedLauncher = Configuration.Uplink;
	if (!IsValid(ResolvedLauncher) && IsValid(GetPlayerAircraft()))
		ResolvedLauncher = GetPlayerAircraft()->FindComponentByClass<UAircraftRadarComponent>();
	if (!IsValid(ResolvedLauncher) && GetOwner() && IsValid(GetOwner()->GetOwner()))
		ResolvedLauncher = GetOwner()->GetOwner()->FindComponentByClass<UAircraftRadarComponent>();
	if (!IsValid(ResolvedLauncher)) ResolvedLauncher = PreviousUplink;
	LaunchRadar = ResolvedLauncher;
	LaunchTrackID = Configuration.LaunchTrackID;
	LaunchContactID = TargetSolution.TargetContactID;
	ResolveLaunchTrackIdentity();
	bMadDogLaunch = LaunchTrackID == -1 && !TargetSolution.bValid &&
		(Configuration.bMadDog || (bHasActiveSeeker && !bNeedsIllumination));
	LastExternalGuidanceRadar = bNeedsIllumination ? Illuminator.Get() :
		(TargetSolution.bMeasured ? ResolveExternalRadarForSolution(TargetSolution) : nullptr);
	LastExternalUpdateWorldTime = LastExternalGuidanceRadar.IsValid() && GetWorld() ?
		GetWorld()->GetTimeSeconds() : -1000000.0f;
	bHadUplink = IsValid(Configuration.Uplink);
	SupportLostSeconds = 0.0f;
	TimeSinceLastDataLink = 0.0f;
	bDataLinkTimeoutNotified = false;
	ObservedChaff.Reset();
	if (bNeedsIllumination && !IsValid(GetLockedTarget()) && IsValid(Illuminator.Get()))
	{
		AActor* Illuminated = Illuminator->GetSTTLockedActor();
		if (!IsValid(Illuminated)) Illuminated = Illuminator->GetContinuousWaveTarget();
		if (IsValid(Illuminated)) LockMissile(Illuminated);
	}
	return true;
}

void URadarMissileGuidanceComponent::ActivateWeapon(bool bActivate)
{
	const bool bWasFired = bWeaponFired;
	Super::ActivateWeapon(bActivate);
	if (!bActivate && bWasFired)
		if (UAircraftRadarComponent* Radar = LaunchRadar.Get())
			Radar->UnregisterLaunchedRadarMissile(this);
}

void URadarMissileGuidanceComponent::HandleProximityFuzeTriggered(AActor* TriggeringActor)
{
	(void)TriggeringActor;
	if (UAircraftRadarComponent* Radar = LaunchRadar.Get())
		Radar->UnregisterLaunchedRadarMissile(this);
}
bool URadarMissileGuidanceComponent::CanFireWeapon() const
{
	if (!bIsWeaponActivated || bWeaponFired) return false;
	if (bNeedsIllumination) return IsValid(GetLockedTarget()) && HasVerifiedIllumination();
	return !bRequireLockToFire || TargetSolution.bValid;
}

bool URadarMissileGuidanceComponent::FireWeapon()
{
	if (!bNeedsIllumination && bHasActiveSeeker) LockedTarget = nullptr;
	if (!CanFireWeapon() || !Super::FireWeapon()) return false;
	// Runtime-added guidance components may launch before their BeginPlay registration.
	if (UAircraftCombatSubsystem* Registry = UAircraftCombatSubsystem::Get(this))
		Registry->RegisterMissileSeeker(this);
	if (bNeedsIllumination)
	{
		FRadarTrack Track;
		if (UAircraftRadarComponent* Radar = Illuminator.Get())
		{
			if (Radar->GetTrackByActor(GetLockedTarget(), Track))
			{
				LastSupportSolution = TargetSolution;
				LastSupportSolution.bValid = true;
				LastSupportSolution.bMeasured = true;
				LastSupportSolution.TargetActor = GetLockedTarget();
				LastSupportSolution.Position = Track.LastKnownPosition;
				LastSupportSolution.Velocity = Track.EstimatedVelocity;
				LastSupportSolution.MeasurementTimeSeconds = GetWorld()->GetTimeSeconds() - Track.TrackAge;
				LastSupportSolution.TargetContactID = Track.ContactID;
				LastSupportSolution.SourceParticipantID = Radar != LaunchRadar.Get() ?
					Radar->GetDataLinkParticipantID() : 0;
				LastSupportSolution.SourceTrackID = Track.TrackID;
				if (!TargetSolution.bValid) TargetSolution = LastSupportSolution;
			}
		}
		SetFlightPhase(ERadarMissileFlightPhase::SemiActive);
		if (UAircraftRadarComponent* Radar = Illuminator.Get())
			Radar->RegisterGuidingMissile(this);
	}
	else
	{
		SetFlightPhase(ERadarMissileFlightPhase::MidCourse);
	}
	if (!LaunchRadar.IsValid()) LaunchRadar = Uplink.IsValid() ? Uplink.Get() :
		(IsValid(GetPlayerAircraft()) ? GetPlayerAircraft()->FindComponentByClass<UAircraftRadarComponent>() : nullptr);
	if (!LaunchRadar.IsValid() && GetOwner() && IsValid(GetOwner()->GetOwner()))
		LaunchRadar = GetOwner()->GetOwner()->FindComponentByClass<UAircraftRadarComponent>();
	if (LaunchContactID <= 0) LaunchContactID = TargetSolution.TargetContactID;
	ResolveLaunchTrackIdentity();
	if (LaunchTrackID == -1 && !TargetSolution.bValid && bHasActiveSeeker && !bNeedsIllumination)
		bMadDogLaunch = true;
	OnProximityFuzeTriggered.AddUniqueDynamic(this,
		&URadarMissileGuidanceComponent::HandleProximityFuzeTriggered);
	if (UAircraftRadarComponent* Radar = LaunchRadar.Get())
		Radar->RegisterLaunchedRadarMissile(this);
	return true;
}

void URadarMissileGuidanceComponent::ReceiveMidCourseUpdate(const FMissileTargetSolution& NewSolution)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	if (bWeaponFired && (bSeekerTransmitting || FlightPhase == ERadarMissileFlightPhase::Unguided)) return;
	if (!NewSolution.bValid || !NewSolution.bMeasured ||
		!FMath::IsFinite(NewSolution.MeasurementTimeSeconds) ||
		!FMath::IsFinite(NewSolution.Position.X) || !FMath::IsFinite(NewSolution.Position.Y) ||
		!FMath::IsFinite(NewSolution.Position.Z) || !FMath::IsFinite(NewSolution.Velocity.X) ||
		!FMath::IsFinite(NewSolution.Velocity.Y) || !FMath::IsFinite(NewSolution.Velocity.Z)) return;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : NewSolution.MeasurementTimeSeconds;
	if (NewSolution.MeasurementTimeSeconds > Now + 0.1f ||
		Now - NewSolution.MeasurementTimeSeconds > MaxMidCourseMeasurementAgeSeconds) return;
	if (TargetSolution.bValid)
	{
		if (NewSolution.MeasurementTimeSeconds < TargetSolution.MeasurementTimeSeconds) return;
		if (IsValid(TargetSolution.TargetActor) &&
			NewSolution.TargetActor != TargetSolution.TargetActor) return;
		if (TargetSolution.TargetContactID > 0 &&
			NewSolution.TargetContactID != TargetSolution.TargetContactID) return;
		const bool bSourceChanged = NewSolution.SourceParticipantID != TargetSolution.SourceParticipantID ||
			NewSolution.SourceTrackID != TargetSolution.SourceTrackID;
		if (bSourceChanged &&
			(TargetSolution.TargetContactID <= 0 || !IsValid(TargetSolution.TargetActor) ||
				NewSolution.TargetActor != TargetSolution.TargetActor)) return;
		const float Advance = FMath::Max(0.0f,
			NewSolution.MeasurementTimeSeconds - TargetSolution.MeasurementTimeSeconds);
		const FVector Predicted = TargetSolution.Position + TargetSolution.Velocity * FMath::Min(Advance, 10.0f);
		if (FVector::Dist(Predicted, NewSolution.Position) >
			MaxMidCourseInnovationCm + Advance * 10000.0f) return;
	}
	TargetSolution = NewSolution;
	TargetSolution.bMeasured = true;
	LastExternalGuidanceRadar = ResolveExternalRadarForSolution(NewSolution);
	LastExternalUpdateWorldTime = Now;
	TimeSinceLastDataLink = 0.0f;
	bDataLinkTimeoutNotified = false;
}

void URadarMissileGuidanceComponent::SetInertialTarget(FVector InPosition, FVector InVelocity)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	TargetSolution.bValid = true;
	TargetSolution.bMeasured = !InVelocity.IsNearlyZero();
	TargetSolution.Position = InPosition;
	TargetSolution.Velocity = InVelocity;
	TargetSolution.MeasurementTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
}

void URadarMissileGuidanceComponent::SetParentRadar(UAircraftRadarComponent* Radar)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	if (bWeaponFired) return;
	Uplink = Radar;
	bHadUplink = IsValid(Radar);
	Illuminator = Radar;
}

void URadarMissileGuidanceComponent::SetRemoteDataLinkSupport(UAircraftRadarComponent* LauncherRadar,
	UAircraftRadarComponent* SourceRadar, int32 SourceParticipantID)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	if (bWeaponFired) return;
	Uplink = LauncherRadar;
	bHadUplink = IsValid(LauncherRadar);
	Illuminator = SourceRadar;
	TargetSolution.SourceParticipantID = SourceParticipantID;
}

void URadarMissileGuidanceComponent::ClearRemoteDataLinkSupport(UAircraftRadarComponent* LauncherRadar)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	if (bWeaponFired) return;
	Uplink = LauncherRadar;
	bHadUplink = IsValid(LauncherRadar);
	Illuminator = LauncherRadar;
	TargetSolution.SourceParticipantID = 0;
	TargetSolution.SourceTrackID = INDEX_NONE;
}

UAircraftRadarComponent* URadarMissileGuidanceComponent::GetParentRadar() const
{
	return bNeedsIllumination ? Illuminator.Get() : Uplink.Get();
}

bool URadarMissileGuidanceComponent::HasLineOfSight(const FVector& From, const AActor* Target,
	const AActor* SourceOwner) const
{
	if (!IsValid(Target) || !GetWorld()) return false;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MissileRadarLineOfSight), false);
	QueryParams.AddIgnoredActor(GetOwner());
	if (SourceOwner) QueryParams.AddIgnoredActor(SourceOwner);
	if (UAircraftRadarComponent* Radar = Illuminator.Get())
		QueryParams.AddIgnoredActor(Radar->GetOwner());
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, From,
		GetTargetTrackingLocation(Target), ECC_Visibility, QueryParams);
	return !bHit || Hit.GetActor() == Target;
}

bool URadarMissileGuidanceComponent::HasVerifiedIllumination() const
{
	const AActor* Target = IsValid(GetLockedTarget()) ? GetLockedTarget() : TargetSolution.TargetActor.Get();
	return HasVerifiedIlluminationFrom(Illuminator.Get(), Target);
}

bool URadarMissileGuidanceComponent::HasVerifiedIlluminationFrom(
	const UAircraftRadarComponent* Radar, const AActor* Target) const
{
	if (!IsValid(Radar) || !IsValid(Target) || !Radar->IsRadarEmitting() ||
		!Radar->IsContinuousWaveIlluminating(Target)) return false;
	FRadarTrack Track;
	if (!Radar->GetTrackByActor(Target, Track) || Track.Status == ERadarTrackStatus::Lost ||
		Track.TrackAge > 1.5f) return false;
	return HasLineOfSight(Radar->GetRadarLocation(), Target, Radar->GetOwner()) &&
		HasLineOfSight(UpdatedComponent ? UpdatedComponent->GetComponentLocation() :
			(GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector), Target);
}

float URadarMissileGuidanceComponent::GetEstimatedTargetRange() const
{
	const FVector MissilePosition = UpdatedComponent ? UpdatedComponent->GetComponentLocation() :
		(GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
	if (FlightPhase == ERadarMissileFlightPhase::TerminalTrack && IsValid(GetLockedTarget()))
		return FVector::Dist(MissilePosition, GetTargetTrackingLocation(GetLockedTarget()));
	if (!TargetSolution.bValid) return TNumericLimits<float>::Max();
	const float Age = GetWorld() ? FMath::Max(0.0f, GetWorld()->GetTimeSeconds() -
		TargetSolution.MeasurementTimeSeconds) : 0.0f;
	return FVector::Dist(MissilePosition, TargetSolution.Position +
		TargetSolution.Velocity * FMath::Min(Age, 120.0f));
}

void URadarMissileGuidanceComponent::ResolveLaunchTrackIdentity()
{
	UAircraftRadarComponent* Radar = LaunchRadar.Get();
	if (LaunchTrackID != -1 || !IsValid(Radar) || !TargetSolution.bMeasured ||
		!IsValid(TargetSolution.TargetActor) || TargetSolution.SourceTrackID <= 0) return;
	FRadarTrack Track;
	bool bFound = false;
	if (TargetSolution.SourceParticipantID <= 0)
	{
		bFound = Radar->GetTrackByID(TargetSolution.SourceTrackID, Track);
	}
	else
	{
		TArray<FRadarTrack> Linked;
		Radar->GetLinkedTracks(Linked);
		for (const FRadarTrack& Candidate : Linked)
		{
			if (Candidate.SourceParticipantID != TargetSolution.SourceParticipantID ||
				Candidate.SourceTrackID != TargetSolution.SourceTrackID) continue;
			Track = Candidate;
			bFound = true;
			break;
		}
	}
	if (!bFound || Track.TrackedActor.Get() != TargetSolution.TargetActor ||
		Track.Status == ERadarTrackStatus::Lost ||
		Track.TrackAge > (Track.Source == ERadarTrackSource::Local ?
			Radar->LocalCorrelationFreshnessSeconds : Radar->DataLinkTrackExpirySeconds)) return;
	LaunchTrackID = Track.TrackID;
	if (LaunchContactID <= 0) LaunchContactID = Track.ContactID;
}

UAircraftRadarComponent* URadarMissileGuidanceComponent::ResolveExternalRadarForSolution(
	const FMissileTargetSolution& Solution) const
{
	if (Solution.SourceParticipantID <= 0)
		return Uplink.IsValid() ? Uplink.Get() : LaunchRadar.Get();
	if (UAircraftRadarComponent* Radar = Illuminator.Get(); IsValid(Radar) &&
		Radar->GetDataLinkParticipantID() == Solution.SourceParticipantID) return Radar;
	if (UAircraftRadarComponent* Radar = Uplink.Get())
	{
		TArray<FRadarTrack> Linked;
		Radar->GetLinkedTracks(Linked);
		for (const FRadarTrack& Track : Linked)
			if (Track.SourceParticipantID == Solution.SourceParticipantID &&
				Track.SourceTrackID == Solution.SourceTrackID)
				return Radar->GetLinkedTrackSource(Track.TrackID);
	}
	return nullptr;
}

UAircraftRadarComponent* URadarMissileGuidanceComponent::GetCurrentExternalGuidanceRadar() const
{
	if (!bWeaponFired || bSeekerTransmitting || FlightPhase == ERadarMissileFlightPhase::Unguided)
		return nullptr;
	if (bNeedsIllumination)
		return FlightPhase == ERadarMissileFlightPhase::SemiActive ? Illuminator.Get() : nullptr;
	if (!bUseMidCourseDataLink || !bHadUplink || !GetWorld() ||
		GetWorld()->GetTimeSeconds() - LastExternalUpdateWorldTime >
		FMath::Max(1.5f, MidCourseUpdateInterval * 2.0f)) return nullptr;
	return LastExternalGuidanceRadar.Get();
}

void URadarMissileGuidanceComponent::FillLaunchedMissileStatus(
	FRadarLaunchedMissileStatus& OutStatus) const
{
	OutStatus.MissileComponent = const_cast<URadarMissileGuidanceComponent*>(this);
	OutStatus.MissileActor = GetOwner();
	OutStatus.LaunchRadar = LaunchRadar.Get();
	OutStatus.CurrentExternalRadar = GetCurrentExternalGuidanceRadar();
	OutStatus.LastExternalRadar = LastExternalGuidanceRadar.Get();
	OutStatus.RelevantRadar = OutStatus.CurrentExternalRadar.Get() ? OutStatus.CurrentExternalRadar.Get() :
		(OutStatus.LastExternalRadar.Get() ? OutStatus.LastExternalRadar.Get() : OutStatus.LaunchRadar.Get());
	OutStatus.LaunchTrackID = LaunchTrackID;
	OutStatus.ContactID = LaunchContactID;
	OutStatus.SourceParticipantID = bNeedsIllumination ?
		LastSupportSolution.SourceParticipantID : TargetSolution.SourceParticipantID;
	OutStatus.SourceTrackID = bNeedsIllumination ?
		LastSupportSolution.SourceTrackID : TargetSolution.SourceTrackID;
	OutStatus.bMadDog = bMadDogLaunch;
	OutStatus.FlightPhase = FlightPhase;
	OutStatus.bOnboardSeekerActive = bSeekerTransmitting;
	OutStatus.FlightTimeSeconds = GetTimeSinceFired();
	if (FlightPhase == ERadarMissileFlightPhase::Unguided)
		OutStatus.GuidanceSource = ERadarMissileGuidanceSource::Unguided;
	else if (bSeekerTransmitting)
		OutStatus.GuidanceSource = ERadarMissileGuidanceSource::OnboardSeeker;
	else if (bNeedsIllumination && OutStatus.CurrentExternalRadar)
		OutStatus.GuidanceSource = ERadarMissileGuidanceSource::Illumination;
	else if (OutStatus.CurrentExternalRadar)
		OutStatus.GuidanceSource = ERadarMissileGuidanceSource::DataLink;
	else
		OutStatus.GuidanceSource = ERadarMissileGuidanceSource::Inertial;
	const UActiveRadarMissileGuidanceComponent* Active =
		Cast<UActiveRadarMissileGuidanceComponent>(this);
	const bool bHasSecondPhase = bHasActiveSeeker &&
		(!Active || Active->SeekerActivation == EActiveSeekerActivation::AtRange);
	if (bHasSecondPhase && bSeekerTransmitting) OutStatus.TimeToActiveSeconds = 0.0f;

	FMissileTargetSolution GuidanceTarget;
	if (!GetGuidanceTargetSolution(GuidanceTarget) || !GuidanceTarget.bValid ||
		!FMath::IsFinite(GuidanceTarget.Position.X) ||
		!FMath::IsFinite(GuidanceTarget.Position.Y) ||
		!FMath::IsFinite(GuidanceTarget.Position.Z) ||
		!FMath::IsFinite(GuidanceTarget.Velocity.X) ||
		!FMath::IsFinite(GuidanceTarget.Velocity.Y) ||
		!FMath::IsFinite(GuidanceTarget.Velocity.Z)) return;
	OutStatus.bHasTargetSolution = true;
	const FVector MissilePosition = UpdatedComponent ? UpdatedComponent->GetComponentLocation() :
		(GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
	const FVector ToTarget = GuidanceTarget.Position - MissilePosition;
	const float Range = ToTarget.Size();
	OutStatus.EstimatedTargetRangeCm = Range;
	if (Range > KINDA_SMALL_NUMBER)
		OutStatus.ClosingSpeedCmPerSecond = FVector::DotProduct(
			Velocity - GuidanceTarget.Velocity, ToTarget / Range);
	if ((IsValid(TargetSolution.TargetActor) || IsValid(GetLockedTarget())) &&
		OutStatus.ClosingSpeedCmPerSecond > 100.0f)
		OutStatus.EstimatedTimeToImpactSeconds = Range / OutStatus.ClosingSpeedCmPerSecond;

	if (!bHasSecondPhase) return;
	if (bSeekerTransmitting || Range <= ActiveSeekerRange)
		OutStatus.TimeToActiveSeconds = 0.0f;
	else if (OutStatus.ClosingSpeedCmPerSecond > 100.0f)
		OutStatus.TimeToActiveSeconds =
			(Range - ActiveSeekerRange) / OutStatus.ClosingSpeedCmPerSecond;
}

void URadarMissileGuidanceComponent::SetFlightPhase(ERadarMissileFlightPhase NewPhase)
{
	FlightPhase = NewPhase;
}

void URadarMissileGuidanceComponent::ActivateOnboardSeeker()
{
	if (!bHasActiveSeeker || bSeekerTransmitting) return;
	bSeekerTransmitting = true;
	ActiveSearchElapsedSeconds = 0.0f;
	SetFlightPhase(ERadarMissileFlightPhase::TerminalSearch);
	if (UAircraftRadarComponent* Radar = Illuminator.Get())
		Radar->UnregisterGuidingMissile(this);
	OnActiveSeekerActivated.Broadcast();
}

bool URadarMissileGuidanceComponent::AcceptCandidate(const AActor* Candidate) const
{
	if (!IsCandidateTargetEligible(Candidate)) return false;
	const UAircraftCombatSettings* Settings = UAircraftCombatSettings::Get();
	const ERadarTargetDomain Domain = Settings ?
		Settings->ResolveTargetDomain(Candidate, true) : ERadarTargetDomain::Air;
	return bSeaTargetsOnly ? Domain == ERadarTargetDomain::Sea : Domain == ERadarTargetDomain::Air;
}

void URadarMissileGuidanceComponent::ScanActiveSeeker(float DeltaTime)
{
	if (!bSeekerTransmitting || !GetWorld()) return;
	FVector SeekerPosition;
	FRotator SeekerRotation;
	GetSeekerTransform(SeekerPosition, SeekerRotation);
	const FVector Forward = SeekerRotation.Vector();
	const float MaxRangeSq = FMath::Square(ActiveSeekerMaxRange);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(ActiveSeekerConeAngle));
	const FVector Predicted = TargetSolution.Position +
		TargetSolution.Velocity * (GetWorld() ?
			FMath::Clamp(GetWorld()->GetTimeSeconds() - TargetSolution.MeasurementTimeSeconds, 0.0f, 120.0f) : 0.0f);
	TArray<AActor*> Candidates;
	if (UAircraftCombatSubsystem* Registry = UAircraftCombatSubsystem::Get(this))
		Registry->GetCombatActorsInVolume(SeekerPosition, ActiveSeekerMaxRange, Candidates);
	if (Candidates.IsEmpty())
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		Objects.AddObjectTypesToQuery(ECC_Vehicle);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MissileRadarAcquisition), false, GetOwner());
		PopulateSeekerIgnoredActors(Query);
		GetWorld()->OverlapMultiByObjectType(Overlaps, SeekerPosition, FQuat::Identity, Objects,
			FCollisionShape::MakeSphere(ActiveSeekerMaxRange), Query);
		for (const FOverlapResult& Overlap : Overlaps)
			if (AActor* Actor = Overlap.GetActor()) Candidates.AddUnique(Actor);
	}
	if (IsValid(GetLockedTarget())) Candidates.AddUnique(GetLockedTarget());
	AActor* Best = nullptr;
	float BestScore = -1.0f;
	float CurrentScore = -1.0f;
	for (AActor* Candidate : Candidates)
	{
		if (!AcceptCandidate(Candidate)) continue;
		bool bChaff = false;
		for (const FName& Tag : ChaffDecoyTags)
		{
			if (Candidate->ActorHasTag(Tag)) { bChaff = true; break; }
		}
		const FVector TargetPosition = GetTargetTrackingLocation(Candidate);
		const FVector ToTarget = TargetPosition - SeekerPosition;
		const float RangeSq = ToTarget.SizeSquared();
		if (RangeSq < 1.0f || RangeSq > MaxRangeSq ||
			FVector::DotProduct(Forward, ToTarget.GetSafeNormal()) < MinDot ||
			!HasLineOfSight(SeekerPosition, Candidate)) continue;
		if (NotchFilterVelocity > 0.0f &&
			FMath::Abs(FVector::DotProduct(Candidate->GetVelocity(), ToTarget.GetSafeNormal())) <
				NotchFilterVelocity) continue;
		const float PredictionError = TargetSolution.bValid ?
			FVector::Dist(TargetPosition, Predicted) : FMath::Sqrt(RangeSq);
		if (TargetSolution.bValid && Candidate != TargetSolution.TargetActor &&
			PredictionError > ActiveSeekerMaxRange * 0.35f) continue;
		bool bSeduce = false;
		if (bChaff && Candidate != GetLockedTarget())
		{
			const TWeakObjectPtr<AActor> ChaffKey(Candidate);
			if (!ObservedChaff.Contains(ChaffKey))
			{
				ObservedChaff.Add(ChaffKey);
				bSeduce = FMath::FRand() < ChaffBreakLockChance;
			}
			if (!bSeduce) continue;
		}
		const float Score = 1.0f / (1.0f + PredictionError / 10000.0f);
		if (Candidate == GetLockedTarget()) CurrentScore = Score;
		if (bSeduce) { BestScore = TNumericLimits<float>::Max(); Best = Candidate; }
		else if (Score > BestScore) { BestScore = Score; Best = Candidate; }
	}
	if (IsValid(GetLockedTarget()))
	{
		if (CurrentScore >= 0.0f)
		{
			TimeSinceSeekerSawTarget = 0.0f;
			if (Best != GetLockedTarget() && BestScore < CurrentScore * TargetSwitchScoreRatio)
				Best = GetLockedTarget();
		}
		else
		{
			TimeSinceSeekerSawTarget += DeltaTime;
			if (TimeSinceSeekerSawTarget <= SeekerMissedScanGraceSeconds) Best = GetLockedTarget();
		}
	}
	if (Best != GetLockedTarget())
	{
		LockMissile(Best);
		if (!Best) OnRadarLockLost.Broadcast();
	}
	if (IsValid(GetLockedTarget()))
	{
		ActiveSearchElapsedSeconds = 0.0f;
		SetFlightPhase(ERadarMissileFlightPhase::TerminalTrack);
	}
	else
	{
		ActiveSearchElapsedSeconds += DeltaTime;
		if (ActiveSearchElapsedSeconds >= ActiveSearchTimeoutSeconds)
		{
			bSeekerTransmitting = false;
			SetFlightPhase(ERadarMissileFlightPhase::Unguided);
		}
		else SetFlightPhase(ERadarMissileFlightPhase::TerminalSearch);
	}
}

void URadarMissileGuidanceComponent::RefreshMidCourseUpdate(float DeltaTime)
{
	if (!bUseMidCourseDataLink || !bHadUplink || bSeekerTransmitting || bNeedsIllumination ||
		!bWeaponFired) return;
	TimeSinceLastDataLink += DeltaTime;
	if (TimeSinceLastDataLink < MidCourseUpdateInterval) return;
	UAircraftRadarComponent* Radar = Uplink.Get();
	AActor* Target = TargetSolution.TargetActor.Get();
	if (IsValid(Radar) && IsValid(Target) && IsValid(Radar->GetOwner()) &&
		FVector::DistSquared(GetOwner()->GetActorLocation(), Radar->GetOwner()->GetActorLocation()) <=
			FMath::Square(DataLinkRange))
	{
		FRadarTrack Track;
		const bool bFresh = Radar->GetBestWeaponSupportTrackForActor(Target, Track);
		// Keep the current donor while its report is fresh. This prevents two donors with
		// alternating transmission slots from bouncing the missile's source every update.
		if (bFresh && Track.Source != ERadarTrackSource::Local &&
			TargetSolution.SourceParticipantID > 0)
		{
			FRadarTrack CurrentDonor;
			if (Radar->GetFreshLinkedTrackForActor(Target, TargetSolution.SourceParticipantID,
				CurrentDonor) && CurrentDonor.TrackAge <= 1.5f) Track = CurrentDonor;
		}
		if (bFresh && Track.Status != ERadarTrackStatus::Lost && Track.TrackAge <= 1.5f)
		{
			FMissileTargetSolution Update = TargetSolution;
			Update.bValid = true;
			Update.bMeasured = true;
			Update.Position = Track.LastKnownPosition;
			Update.Velocity = Track.EstimatedVelocity;
			Update.MeasurementTimeSeconds = GetWorld()->GetTimeSeconds() - Track.TrackAge;
			Update.TargetContactID = Track.ContactID;
			Update.SourceParticipantID = Track.Source == ERadarTrackSource::Local ? 0 : Track.SourceParticipantID;
			Update.SourceTrackID = Track.Source == ERadarTrackSource::Local ? Track.TrackID : Track.SourceTrackID;
			ReceiveMidCourseUpdate(Update);
			if (TargetSolution.MeasurementTimeSeconds == Update.MeasurementTimeSeconds &&
				TargetSolution.SourceParticipantID == Update.SourceParticipantID &&
				TargetSolution.SourceTrackID == Update.SourceTrackID) return;
		}
	}
	if (!bDataLinkTimeoutNotified && TimeSinceLastDataLink >= MidCourseUpdateInterval * 3.0f)
	{
		bDataLinkTimeoutNotified = true;
		OnDataLinkTimeout.Broadcast(TimeSinceLastDataLink);
	}
}

bool URadarMissileGuidanceComponent::GetGuidanceTargetSolution(FMissileTargetSolution& OutSolution) const
{
	if (FlightPhase == ERadarMissileFlightPhase::Unguided) return false;
	if (bSeekerTransmitting && IsValid(GetLockedTarget()))
	{
		OutSolution = FMissileTargetSolution();
		OutSolution.bValid = true;
		OutSolution.bMeasured = true;
		OutSolution.TargetActor = GetLockedTarget();
		OutSolution.Position = GetTargetTrackingLocation(GetLockedTarget());
		OutSolution.Velocity = GetLockedTarget()->GetVelocity();
		return true;
	}
	OutSolution = bNeedsIllumination && !bSeekerTransmitting ?
		LastSupportSolution : TargetSolution;
	if (!OutSolution.bValid) return false;
	if (OutSolution.bMeasured && GetWorld())
		OutSolution.Position += OutSolution.Velocity *
			FMath::Clamp(GetWorld()->GetTimeSeconds() - OutSolution.MeasurementTimeSeconds, 0.0f, 120.0f);
	return true;
}

void URadarMissileGuidanceComponent::TickSeekerLogic(float DeltaTime)
{
	if (!bWeaponFired) return;
	if (FlightPhase == ERadarMissileFlightPhase::Unguided) return;
	if (bNeedsIllumination && !bSeekerTransmitting)
	{
		if (!HasVerifiedIllumination() && IsValid(Uplink.Get()) &&
			Uplink.Get() != Illuminator.Get() &&
			HasVerifiedIlluminationFrom(Uplink.Get(), GetLockedTarget()))
		{
			if (UAircraftRadarComponent* Old = Illuminator.Get()) Old->UnregisterGuidingMissile(this);
			Illuminator = Uplink;
			LastExternalGuidanceRadar = Uplink;
			LastExternalUpdateWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
			Uplink->RegisterGuidingMissile(this);
		}
		if (HasVerifiedIllumination())
		{
			SupportLostSeconds = 0.0f;
			FRadarTrack Track;
			if (UAircraftRadarComponent* Radar = Illuminator.Get())
			{
				if (Radar->GetTrackByActor(GetLockedTarget(), Track))
				{
					LastSupportSolution = TargetSolution;
					LastSupportSolution.bValid = true;
					LastSupportSolution.bMeasured = true;
					LastSupportSolution.TargetActor = GetLockedTarget();
					LastSupportSolution.Position = Track.LastKnownPosition;
					LastSupportSolution.Velocity = Track.EstimatedVelocity;
					LastSupportSolution.MeasurementTimeSeconds = GetWorld()->GetTimeSeconds() - Track.TrackAge;
					LastSupportSolution.TargetContactID = Track.ContactID;
					LastSupportSolution.SourceParticipantID = Radar != LaunchRadar.Get() ?
						Radar->GetDataLinkParticipantID() : 0;
					LastSupportSolution.SourceTrackID = Track.TrackID;
				}
			}
			SetFlightPhase(ERadarMissileFlightPhase::SemiActive);
		}
		else
		{
			SupportLostSeconds += DeltaTime;
			if (bHasActiveSeeker && GetEstimatedTargetRange() <= ActiveSeekerRange)
				ActivateOnboardSeeker();
			else if (SupportLostSeconds <= RequiredSupportCoastSeconds &&
				LastSupportSolution.bValid)
				SetFlightPhase(ERadarMissileFlightPhase::Coasting);
			else
			{
				SetFlightPhase(ERadarMissileFlightPhase::Unguided);
				LockMissile(nullptr);
				if (UAircraftRadarComponent* Radar = Illuminator.Get())
					Radar->UnregisterGuidingMissile(this);
				OnRadarLockLost.Broadcast();
			}
		}
	}
	else RefreshMidCourseUpdate(DeltaTime);
	if (bHasActiveSeeker && !bSeekerTransmitting &&
		GetEstimatedTargetRange() <= ActiveSeekerRange && ActiveSeekerRange > 0.0f)
		ActivateOnboardSeeker();
	if (bSeekerTransmitting) ScanActiveSeeker(DeltaTime);
}

UActiveRadarMissileGuidanceComponent::UActiveRadarMissileGuidanceComponent()
{
	bHasActiveSeeker = true;
}

bool UActiveRadarMissileGuidanceComponent::FireWeapon()
{
	if (!Super::FireWeapon()) return false;
	if (SeekerActivation == EActiveSeekerActivation::Immediate ||
		(!TargetSolution.bValid && !IsValid(GetLockedTarget())))
		ActivateOnboardSeeker();
	return true;
}

void UActiveRadarMissileGuidanceComponent::TickSeekerLogic(float DeltaTime)
{
	Super::TickSeekerLogic(DeltaTime);
	if (bWeaponFired && SeekerActivation == EActiveSeekerActivation::Immediate &&
		!bSeekerTransmitting && FlightPhase != ERadarMissileFlightPhase::Unguided)
		ActivateOnboardSeeker();
}

USemiActiveRadarMissileGuidanceComponent::USemiActiveRadarMissileGuidanceComponent()
{
	bNeedsIllumination = true;
	bUseMidCourseDataLink = false;
}

UHybridRadarMissileGuidanceComponent::UHybridRadarMissileGuidanceComponent()
{
	bHasActiveSeeker = true;
	bNeedsIllumination = true;
	bUseMidCourseDataLink = false;
}

UAntiShipMissileGuidanceComponent::UAntiShipMissileGuidanceComponent()
{
	bHasActiveSeeker = true;
	bSeaTargetsOnly = true;
	NotchFilterVelocity = 0.0f;
}

void UAntiShipMissileGuidanceComponent::TickSeekerLogic(float DeltaTime)
{
	Super::TickSeekerLogic(DeltaTime);
	if (bWeaponFired && bSeaSkim && !bSeekerTransmitting &&
		FlightPhase == ERadarMissileFlightPhase::MidCourse)
		SetFlightPhase(ERadarMissileFlightPhase::SeaSkim);
}

bool UAntiShipMissileGuidanceComponent::GetGuidanceTargetSolution(FMissileTargetSolution& OutSolution) const
{
	if (!Super::GetGuidanceTargetSolution(OutSolution)) return false;
	if (bSeaSkim && !bSeekerTransmitting)
	{
		OutSolution.Position.Z = SeaSurfaceWorldZ + SeaSkimHeight;
		OutSolution.bMeasured = false;
	}
	return true;
}
