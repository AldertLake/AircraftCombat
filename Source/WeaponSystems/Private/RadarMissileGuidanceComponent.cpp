// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "RadarMissileGuidanceComponent.h"
#include "AircraftRadarComponent.h"
#include "Weapon.h"
#include "AircraftCombatSubsystem.h"
#include "AircraftCombatDebug.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"

URadarMissileGuidanceComponent::URadarMissileGuidanceComponent()
{
	// Radar missiles typically track the aircraft body center, not the engine exhaust
	TargetTrackingSocket = NAME_None;
	SetIsReplicatedByDefault(true);
}

void URadarMissileGuidanceComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UAircraftCombatSubsystem* CombatSubsystem = World->GetSubsystem<UAircraftCombatSubsystem>())
		{
			CombatSubsystem->RegisterRadarMissile(this);
		}
	}
}

void URadarMissileGuidanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UAircraftCombatSubsystem* CombatSubsystem = World->GetSubsystem<UAircraftCombatSubsystem>())
		{
			CombatSubsystem->UnregisterRadarMissile(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void URadarMissileGuidanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(URadarMissileGuidanceComponent, FlightPhase, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(URadarMissileGuidanceComponent, InertialTargetLocation, COND_OwnerOnly);
}

bool URadarMissileGuidanceComponent::CanFireWeapon() const
{
	if (!bIsWeaponActivated)
	{
		return false;
	}

	if (!bRequireLockToFire)
	{
		return true;
	}

	// ARH and INS+Terminal modes can fire with just inertial target data (no live lock required)
	if (GuidanceMode == ERadarMissileGuidanceMode::ActiveRadarHoming ||
		GuidanceMode == ERadarMissileGuidanceMode::InertialWithTerminal)
	{
		return IsValid(GetLockedTarget()) || !InertialTargetLocation.IsNearlyZero();
	}

	// SARH requires a locked target and parent radar illumination
	if (GuidanceMode == ERadarMissileGuidanceMode::SemiActiveRadarHoming)
	{
		return IsValid(GetLockedTarget()) && CheckParentRadarIllumination();
	}

	// Passive homing requires a valid target (radar emitter)
	return IsValid(GetLockedTarget());
}

void URadarMissileGuidanceComponent::ReceiveMidCourseUpdate(FVector TargetPos, FVector TargetVel)
{
	InertialTargetLocation = TargetPos;
	InertialTargetVelocity = TargetVel;
	TimeSinceLastDataLink = 0.0f;
	bDataLinkTimedOut = false;

	OnMidCourseUpdateReceived.Broadcast();
}

void URadarMissileGuidanceComponent::SetInertialTarget(FVector InTargetLocation, FVector InTargetVelocity)
{
	InertialTargetLocation = InTargetLocation;
	InertialTargetVelocity = InTargetVelocity;
}

void URadarMissileGuidanceComponent::SetParentRadar(UAircraftRadarComponent* InRadar)
{
	ParentRadarComponent = InRadar;
}

UAircraftRadarComponent* URadarMissileGuidanceComponent::GetParentRadar() const
{
	return ParentRadarComponent.IsValid() ? ParentRadarComponent.Get() : nullptr;
}

bool URadarMissileGuidanceComponent::IsDataLinkActive() const
{
	if (bDataLinkTimedOut)
	{
		return false;
	}

	const float TimeoutThreshold = MidCourseUpdateInterval * DataLinkTimeoutMultiplier;
	return TimeSinceLastDataLink < TimeoutThreshold;
}

bool URadarMissileGuidanceComponent::CheckParentRadarIllumination() const
{
	UAircraftRadarComponent* Radar = GetParentRadar();
	if (!Radar)
	{
		return false;
	}

	// Check if the parent radar is in STT mode and locked onto our designated target
	return Radar->IsSTTLocked() && Radar->GetSTTLockedActor() == GetLockedTarget();
}

void URadarMissileGuidanceComponent::TransitionToPhase(ERadarMissileFlightPhase NewPhase)
{
	if (FlightPhase == NewPhase)
	{
		return;
	}

	const ERadarMissileFlightPhase OldPhase = FlightPhase;
	FlightPhase = NewPhase;

	switch (NewPhase)
	{
		case ERadarMissileFlightPhase::Terminal:
			if (!bActiveSeekerBroadcast)
			{
				bActiveSeekerBroadcast = true;
				OnActiveSeekerActivated.Broadcast();
			}
			break;

		case ERadarMissileFlightPhase::SeaSkim:
			if (!bActiveSeekerBroadcast)
			{
				bActiveSeekerBroadcast = true;
				OnActiveSeekerActivated.Broadcast();
			}
			OnSeaSkimActivated.Broadcast();
			break;

		case ERadarMissileFlightPhase::Autonomous:
			OnDataLinkTimeout.Broadcast(TimeSinceLastDataLink);
			break;

		default:
			break;
	}
}

FVector URadarMissileGuidanceComponent::ComputeLoftDirection(const FVector& ForwardDir, const FVector& ToTarget) const
{
	// Compute a lofted direction by rotating the forward vector upward by LoftAngle
	const FVector Right = FVector::CrossProduct(ForwardDir, FVector::UpVector).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		return ForwardDir;
	}

	const FQuat LoftRotation = FQuat(Right, FMath::DegreesToRadians(-LoftAngle)); // Negative = pitch up
	return (LoftRotation * ForwardDir).GetSafeNormal();
}

void URadarMissileGuidanceComponent::UpdateInertialDeadReckoning(float DeltaTime)
{
	// Extrapolate target position using last known velocity (dead reckoning)
	if (!InertialTargetVelocity.IsNearlyZero())
	{
		InertialTargetLocation += InertialTargetVelocity * DeltaTime;
	}
}

void URadarMissileGuidanceComponent::CheckDopplerNotch()
{
	if (NotchFilterVelocity <= 0.0f || !IsValid(GetLockedTarget()))
	{
		return;
	}

	const FVector MissileLocation = UpdatedComponent
		? UpdatedComponent->GetComponentLocation()
		: (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
	const FVector TargetLocation = GetTargetTrackingLocation(GetLockedTarget());
	const FVector R = TargetLocation - MissileLocation;
	const float Range = R.Size();

	if (Range < KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector R_Hat = R / Range;
	const FVector TargetVelocity = GetLockedTarget()->GetVelocity();
	const FVector MissileVelocity = Velocity;
	const FVector V_Rel = TargetVelocity - MissileVelocity;

	// Radial (closure) velocity — target must have some radial component for Doppler detection
	const float ClosureRate = -FVector::DotProduct(R_Hat, V_Rel);

	if (FMath::Abs(ClosureRate) < NotchFilterVelocity)
	{
		// Target is in the Doppler notch — radar cannot distinguish target from ground clutter
		OnTargetNotching.Broadcast();

		if (FlightPhase == ERadarMissileFlightPhase::Terminal)
		{
			// In terminal phase, losing Doppler means we lose the target
			AActor* LostTarget = GetLockedTarget();
			LockMissile(nullptr);
			OnRadarLockLost.Broadcast();
			OnTargetLockLost.Broadcast(LostTarget);
		}
	}
}

void URadarMissileGuidanceComponent::CheckChaffCountermeasures()
{
	if (ChaffBreakLockChance <= 0.0f || ChaffDecoyTags.Num() == 0 || !IsValid(GetLockedTarget()))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);

	// Check for chaff in a sphere around the missile seeker
	const float ChaffCheckRadius = ActiveSeekerConeAngle > 0.0f ? ActiveSeekerMaxRange * 0.1f : ProximityFuzeRadius * 5.0f;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RadarMissileChaffCheck), false, GetOwner());
	PopulateSeekerIgnoredActors(QueryParams);

	TArray<FOverlapResult> OverlapResults;
	const FCollisionShape SphereShape = FCollisionShape::MakeSphere(ChaffCheckRadius);
	const bool bHasOverlaps = World->OverlapMultiByChannel(
		OverlapResults,
		SeekerLocation,
		FQuat::Identity,
		DetectionChannel,
		SphereShape,
		QueryParams
	);

	if (!bHasOverlaps)
	{
		return;
	}

	for (const FOverlapResult& Overlap : OverlapResults)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!IsValid(Candidate))
		{
			continue;
		}

		bool bIsChaff = false;
		for (const FName& Tag : ChaffDecoyTags)
		{
			if (Candidate->ActorHasTag(Tag))
			{
				bIsChaff = true;
				break;
			}
		}

		if (bIsChaff)
		{
			OnChaffDeployedNearby.Broadcast(ChaffBreakLockChance);

			if (FMath::FRand() < ChaffBreakLockChance)
			{
				// Chaff successfully broke the lock
				AActor* LostTarget = GetLockedTarget();
				LockMissile(nullptr);
				OnRadarLockLost.Broadcast();
				OnTargetLockLost.Broadcast(LostTarget);
				return;
			}
		}
	}
}

void URadarMissileGuidanceComponent::PerformActiveSeekerScan()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);

	const FVector SeekerForward = SeekerRotation.Vector();
	const float ConeHalfAngleRad = FMath::DegreesToRadians(ActiveSeekerConeAngle);
	const float CosConeHalfAngle = FMath::Cos(ConeHalfAngleRad);
	const float MaxRangeSq = FMath::Square(ActiveSeekerMaxRange);

	if (bEnableDebugTraces)
	{
		DrawDebugCone(World, SeekerLocation, SeekerForward, ActiveSeekerMaxRange * 0.15f, ConeHalfAngleRad, ConeHalfAngleRad, 16, FColor::Cyan, false, -1.0f, 0, 1.0f);
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RadarMissileActiveSeekerScan), false, GetOwner());
	PopulateSeekerIgnoredActors(QueryParams);

	TArray<AActor*> Candidates;
	if (UAircraftCombatSubsystem* CombatSubsystem = World->GetSubsystem<UAircraftCombatSubsystem>())
	{
		TArray<APawn*> Pawns;
		CombatSubsystem->GetCombatPawnsInRange(SeekerLocation, ActiveSeekerMaxRange, Pawns);
		for (APawn* P : Pawns)
		{
			if (P)
			{
				Candidates.Add(P);
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		TArray<FOverlapResult> OverlapResults;
		const FCollisionShape SphereShape = FCollisionShape::MakeSphere(ActiveSeekerMaxRange);
		bool bHasOverlaps = false;

		if (bQueryAllDynamicObjects)
		{
			FCollisionObjectQueryParams ObjectParams;
			ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
			ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
			ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
			ObjectParams.AddObjectTypesToQuery(ECC_Vehicle);

			bHasOverlaps = World->OverlapMultiByObjectType(
				OverlapResults,
				SeekerLocation,
				FQuat::Identity,
				ObjectParams,
				SphereShape,
				QueryParams
			);

			if (OverlapResults.IsEmpty())
			{
				bHasOverlaps = World->OverlapMultiByChannel(
					OverlapResults,
					SeekerLocation,
					FQuat::Identity,
					DetectionChannel,
					SphereShape,
					QueryParams
				);
			}
		}
		else
		{
			bHasOverlaps = World->OverlapMultiByChannel(
				OverlapResults,
				SeekerLocation,
				FQuat::Identity,
				DetectionChannel,
				SphereShape,
				QueryParams
			);
		}

		if (bHasOverlaps)
		{
			for (const FOverlapResult& Overlap : OverlapResults)
			{
				if (AActor* Candidate = Overlap.GetActor())
				{
					Candidates.Add(Candidate);
				}
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		return;
	}

	AActor* BestTarget = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	TSet<AActor*> ProcessedActors;
	for (AActor* Candidate : Candidates)
	{
		if (!Candidate || ProcessedActors.Contains(Candidate))
		{
			continue;
		}
		ProcessedActors.Add(Candidate);

		if (!IsCandidateTargetEligible(Candidate))
		{
			continue;
		}

		const FVector TargetLocation = GetTargetTrackingLocation(Candidate);
		const FVector ToTarget = TargetLocation - SeekerLocation;
		const float DistSq = ToTarget.SizeSquared();

		if (DistSq <= KINDA_SMALL_NUMBER || DistSq > MaxRangeSq)
		{
			continue;
		}

		const FVector ToTargetDir = ToTarget.GetSafeNormal();
		const float DotProduct = FVector::DotProduct(SeekerForward, ToTargetDir);

		if (DotProduct < CosConeHalfAngle)
		{
			continue;
		}

		// LOS check
		FHitResult HitResult;
		const bool bHit = World->LineTraceSingleByChannel(HitResult, SeekerLocation, TargetLocation, ECC_Visibility, QueryParams);
		const bool bLOSClear = !bHit || (HitResult.GetActor() == Candidate);

		if (bLOSClear && DistSq < BestDistanceSq)
		{
			BestDistanceSq = DistSq;
			BestTarget = Candidate;
		}

		if (bEnableDebugTraces)
		{
			const FColor LineColor = bLOSClear ? FColor::Green : FColor::Red;
			DrawDebugLine(World, SeekerLocation, TargetLocation, LineColor, false, -1.0f, 0, 1.0f);
		}
	}

	if (BestTarget && BestTarget != GetLockedTarget())
	{
		LockMissile(BestTarget);
		OnTargetLocked.Broadcast(BestTarget);
	}
}

// Guidance mode tick implementations
void URadarMissileGuidanceComponent::TickARHGuidance(float DeltaTime)
{
	TimeSinceLastDataLink += DeltaTime;

	const FVector MissileLocation = UpdatedComponent
		? UpdatedComponent->GetComponentLocation()
		: (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);

	// Update inertial dead reckoning
	UpdateInertialDeadReckoning(DeltaTime);

	// Calculate range to target (use locked target if valid, otherwise use inertial)
	FVector TargetPos = IsValid(GetLockedTarget()) ? GetTargetTrackingLocation(GetLockedTarget()) : InertialTargetLocation;
	const float RangeToTarget = FVector::Dist(MissileLocation, TargetPos);

	// Check datalink timeout
	if (!bDataLinkTimedOut && !IsDataLinkActive() && FlightPhase == ERadarMissileFlightPhase::MidCourse)
	{
		bDataLinkTimedOut = true;
		TransitionToPhase(ERadarMissileFlightPhase::Autonomous);
	}

	// Phase transitions
	switch (FlightPhase)
	{
		case ERadarMissileFlightPhase::PreLaunch:
			if (bWeaponFired)
			{
				// If launched with no lock and no inertial waypoint (Maddog / Boresight), activate seeker immediately
				if (!IsValid(GetLockedTarget()) && InertialTargetLocation.IsNearlyZero())
				{
					TransitionToPhase(ERadarMissileFlightPhase::Terminal);
				}
				else
				{
					TransitionToPhase(ERadarMissileFlightPhase::MidCourse);
				}
			}
			break;

		case ERadarMissileFlightPhase::MidCourse:
		case ERadarMissileFlightPhase::Autonomous:
		{
			// Check for loft
			if (bEnableLoft && !bLoftComplete && RangeToTarget > LoftActivationRange)
			{
				TransitionToPhase(ERadarMissileFlightPhase::Loft);
				break;
			}

			// Check for terminal seeker activation (pitbull range)
			if (RangeToTarget <= ActiveSeekerRange && ActiveSeekerRange > 0.0f)
			{
				TransitionToPhase(ERadarMissileFlightPhase::Terminal);
				break;
			}

			// During mid-course, guide toward inertial target
			if (!InertialTargetLocation.IsNearlyZero() && !IsValid(GetLockedTarget()))
			{
				// Create a temporary "virtual target" for PN guidance
				// We use the inertial position to set the locked target location for guidance
				// The base class PN will handle steering if we have a locked target
			}
			break;
		}

		case ERadarMissileFlightPhase::Loft:
		{
			if (RangeToTarget <= LoftTerminationRange || RangeToTarget <= ActiveSeekerRange)
			{
				bLoftComplete = true;
				TransitionToPhase(ERadarMissileFlightPhase::Terminal);
			}
			break;
		}

		case ERadarMissileFlightPhase::Terminal:
		{
			// Active seeker scan
			PerformActiveSeekerScan();

			// Check Doppler notch
			CheckDopplerNotch();

			// Check chaff
			CheckChaffCountermeasures();
			break;
		}

		default:
			break;
	}

	// Request datalink updates from parent radar
	if (FlightPhase == ERadarMissileFlightPhase::MidCourse && IsValid(GetLockedTarget()))
	{
		UAircraftRadarComponent* Radar = GetParentRadar();
		if (Radar && TimeSinceLastDataLink >= MidCourseUpdateInterval)
		{
			// Check if parent is still within datalink range
			AActor* ParentOwner = Radar->GetOwner();
			if (IsValid(ParentOwner))
			{
				const float DistToParent = FVector::Dist(MissileLocation, ParentOwner->GetActorLocation());
				if (DistToParent <= DataLinkRange)
				{
					// Auto-receive update from parent radar's tracked target
					FRadarTrack TargetTrack;
					if (Radar->GetTrackByActor(GetLockedTarget(), TargetTrack) && TargetTrack.Status != ERadarTrackStatus::Lost)
					{
						ReceiveMidCourseUpdate(TargetTrack.LastKnownPosition, TargetTrack.EstimatedVelocity);
					}
				}
			}
		}
	}

	// Debug overlay
	if (bEnableDebugTraces && GEngine)
	{
		const UEnum* PhaseEnum = StaticEnum<ERadarMissileFlightPhase>();
		const FString PhaseName = PhaseEnum ? PhaseEnum->GetDisplayNameTextByValue(static_cast<int64>(FlightPhase)).ToString() : TEXT("Unknown");

		const float ClosureRate = -FVector::DotProduct(
			(InertialTargetLocation - MissileLocation).GetSafeNormal(),
			InertialTargetVelocity - Velocity
		);

		FAircraftCombatDebug::PrintMissileTelemetry(
			GetOwner() ? GetOwner()->GetName() : GetName(),
			PhaseName,
			ClosureRate,
			RangeToTarget / 100.0f,
			FlightPhase == ERadarMissileFlightPhase::Terminal,
			IsDataLinkActive()
		);

		if (!InertialTargetLocation.IsNearlyZero())
		{
			if (UWorld* World = GetWorld())
			{
				DrawDebugSphere(World, InertialTargetLocation, 150.0f, 12, FColor::Blue, false, -1.0f, 0, 2.0f);
				DrawDebugLine(World, MissileLocation, InertialTargetLocation, FColor::Blue, false, -1.0f, 0, 1.0f);
			}
		}
	}
}

void URadarMissileGuidanceComponent::TickSARHGuidance(float DeltaTime)
{
	// SARH missiles require continuous parent radar illumination
	if (!bWeaponFired)
	{
		return;
	}

	if (FlightPhase == ERadarMissileFlightPhase::PreLaunch)
	{
		TransitionToPhase(ERadarMissileFlightPhase::Terminal);
	}

	// Verify parent radar is still illuminating our target
	if (!CheckParentRadarIllumination())
	{
		// Parent radar broke STT lock — missile loses guidance
		if (IsValid(GetLockedTarget()))
		{
			AActor* LostTarget = GetLockedTarget();
			LockMissile(nullptr);
			OnRadarLockLost.Broadcast();
			OnTargetLockLost.Broadcast(LostTarget);
		}
	}
	else
	{
		// Check Doppler notch
		CheckDopplerNotch();

		// Check chaff
		CheckChaffCountermeasures();
	}

	if (bEnableDebugTraces && GEngine)
	{
		const bool bIlluminated = CheckParentRadarIllumination();
		const float TargetDist = IsValid(GetLockedTarget())
			? FVector::Dist(UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector, GetLockedTarget()->GetActorLocation()) / 100.0f
			: 0.0f;

		FAircraftCombatDebug::PrintMissileTelemetry(
			GetOwner() ? GetOwner()->GetName() : GetName(),
			bIlluminated ? TEXT("SARH-Tracking") : TEXT("SARH-Lost"),
			0.0f,
			TargetDist,
			bIlluminated,
			bIlluminated
		);

		// Draw illumination line from parent to target
		UAircraftRadarComponent* Radar = GetParentRadar();
		if (Radar && IsValid(Radar->GetOwner()) && IsValid(GetLockedTarget()))
		{
			if (UWorld* World = GetWorld())
			{
				DrawDebugLine(World, Radar->GetOwner()->GetActorLocation(), GetTargetTrackingLocation(GetLockedTarget()), FColor::Yellow, false, -1.0f, 0, 2.0f);
			}
		}
	}
}

void URadarMissileGuidanceComponent::TickPassiveGuidance(float DeltaTime)
{
	if (!bWeaponFired)
	{
		return;
	}

	if (FlightPhase == ERadarMissileFlightPhase::PreLaunch)
	{
		TransitionToPhase(ERadarMissileFlightPhase::Terminal);
	}

	// Passive homing: Track toward the locked target (assumed to be a radar emitter)
	// The target should be an actor with a UAircraftRadarComponent that is actively emitting
	if (IsValid(GetLockedTarget()))
	{
		// Check if the target's radar is still emitting
		UAircraftRadarComponent* TargetRadar = GetLockedTarget()->FindComponentByClass<UAircraftRadarComponent>();
		if (TargetRadar)
		{
			// Passive homing only works if the target radar is emitting (not in Off or Standby)
			if (!TargetRadar->IsRadarEmitting())
			{
				// Target shut down their radar — we lose the signal
				AActor* LostTarget = GetLockedTarget();
				LockMissile(nullptr);
				OnRadarLockLost.Broadcast();
				OnTargetLockLost.Broadcast(LostTarget);

				// Fall back to last known position
				InertialTargetLocation = LostTarget->GetActorLocation();
			}
		}
	}
	else if (!InertialTargetLocation.IsNearlyZero())
	{
		// Flying toward last known emitter position — try to reacquire
		UpdateInertialDeadReckoning(DeltaTime);
	}

	if (bEnableDebugTraces && GEngine)
	{
		const bool bHasTarget = IsValid(GetLockedTarget());
		const float TargetDist = bHasTarget
			? FVector::Dist(UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector, GetLockedTarget()->GetActorLocation()) / 100.0f
			: (!InertialTargetLocation.IsNearlyZero() ? FVector::Dist(UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector, InertialTargetLocation) / 100.0f : 0.0f);

		FAircraftCombatDebug::PrintMissileTelemetry(
			GetOwner() ? GetOwner()->GetName() : GetName(),
			bHasTarget ? TEXT("Passive-Tracking") : TEXT("Passive-Inertial"),
			0.0f,
			TargetDist,
			bHasTarget,
			false
		);
	}
}

void URadarMissileGuidanceComponent::TickInertialTerminalGuidance(float DeltaTime)
{
	TimeSinceLastDataLink += DeltaTime;

	const FVector MissileLocation = UpdatedComponent
		? UpdatedComponent->GetComponentLocation()
		: (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);

	UpdateInertialDeadReckoning(DeltaTime);

	const float RangeToTarget = FVector::Dist(MissileLocation, InertialTargetLocation);

	switch (FlightPhase)
	{
		case ERadarMissileFlightPhase::PreLaunch:
			if (bWeaponFired)
			{
				TransitionToPhase(ERadarMissileFlightPhase::MidCourse);
			}
			break;

		case ERadarMissileFlightPhase::MidCourse:
		{
			// Check if we've reached the terminal seeker activation range
			if (RangeToTarget <= ActiveSeekerRange && ActiveSeekerRange > 0.0f)
			{
				if (bEnableSeaSkimming)
				{
					TransitionToPhase(ERadarMissileFlightPhase::SeaSkim);
				}
				else
				{
					TransitionToPhase(ERadarMissileFlightPhase::Terminal);
				}
			}
			break;
		}

		case ERadarMissileFlightPhase::SeaSkim:
		{
			// Active seeker scan while maintaining sea-skim altitude
			PerformActiveSeekerScan();

			// Altitude hold
			if (UpdatedComponent)
			{
				const FVector CurrentPos = UpdatedComponent->GetComponentLocation();
				// Simple altitude correction toward SeaSkimAltitude
				// Note: SeaSkimAltitude is relative to sea level (assumed Z=0 in most scenarios)
				if (CurrentPos.Z > SeaSkimAltitude + 200.0f)
				{
					// Push the missile down gently
					Velocity.Z -= 500.0f * DeltaTime;
				}
				else if (CurrentPos.Z < SeaSkimAltitude - 100.0f)
				{
					Velocity.Z += 300.0f * DeltaTime;
				}
			}
			break;
		}

		case ERadarMissileFlightPhase::Terminal:
		{
			PerformActiveSeekerScan();
			CheckDopplerNotch();
			break;
		}

		default:
			break;
	}

	if (bEnableDebugTraces && GEngine)
	{
		const UEnum* PhaseEnum = StaticEnum<ERadarMissileFlightPhase>();
		const FString PhaseName = PhaseEnum ? PhaseEnum->GetDisplayNameTextByValue(static_cast<int64>(FlightPhase)).ToString() : TEXT("Unknown");

		FAircraftCombatDebug::PrintMissileTelemetry(
			GetOwner() ? GetOwner()->GetName() : GetName(),
			PhaseName,
			0.0f,
			RangeToTarget / 100.0f,
			FlightPhase == ERadarMissileFlightPhase::Terminal || FlightPhase == ERadarMissileFlightPhase::SeaSkim,
			false
		);
	}
}

// Seeker logic dispatcher
void URadarMissileGuidanceComponent::TickSeekerLogic(float DeltaTime)
{
	switch (GuidanceMode)
	{
		case ERadarMissileGuidanceMode::ActiveRadarHoming:
			TickARHGuidance(DeltaTime);
			break;

		case ERadarMissileGuidanceMode::SemiActiveRadarHoming:
			TickSARHGuidance(DeltaTime);
			break;

		case ERadarMissileGuidanceMode::PassiveRadarHoming:
			TickPassiveGuidance(DeltaTime);
			break;

		case ERadarMissileGuidanceMode::InertialWithTerminal:
			TickInertialTerminalGuidance(DeltaTime);
			break;
	}

	// Loft override — when in loft phase, modify the velocity direction upward
	if (FlightPhase == ERadarMissileFlightPhase::Loft && bWeaponFired)
	{
		const FVector ForwardDir = Velocity.IsNearlyZero() ? FVector::ForwardVector : Velocity.GetSafeNormal();
		const FVector TargetPos = IsValid(GetLockedTarget()) ? GetTargetTrackingLocation(GetLockedTarget()) : InertialTargetLocation;
		const FVector ToTarget = (TargetPos - (UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector)).GetSafeNormal();

		const FVector LoftDir = ComputeLoftDirection(ForwardDir, ToTarget);
		const float CurrentSpeed = Velocity.Size();

		// Blend toward loft direction
		const FVector BlendedDir = FMath::VInterpNormalRotationTo(ForwardDir, LoftDir, DeltaTime, MaxTurnRate * 0.5f);
		Velocity = BlendedDir * CurrentSpeed;
		UpdateComponentVelocity();

		if (bEnableDebugTraces)
		{
			if (UWorld* World = GetWorld())
			{
				const FVector MissilePos = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
				DrawDebugLine(World, MissilePos, MissilePos + LoftDir * 5000.0f, FColor::Purple, false, -1.0f, 0, 2.0f);
			}
		}
	}
}
