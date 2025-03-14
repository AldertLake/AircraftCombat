// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "ARMMissileGuidanceComponent.h"
#include "AircraftRadarComponent.h"
#include "RadarWarningReceiverComponent.h"
#include "AircraftCombatSubsystem.h"
#include "AircraftCombatDebug.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"

UARMMissileGuidanceComponent::UARMMissileGuidanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);

	// SEAD anti-radiation kinematic defaults (high speed kinetic energy)
	MaxCruiseSpeed = 42000.0f;          // ~Mach 3.5 at sea level (~1200 m/s)
	MotorAcceleration = 8000.0f;        // Rapid acceleration
	MotorIgnitionDelay = 0.25f;
	GuidanceActivationDelay = 0.4f;
	NavigationGain = 4.0f;
	MaxLateralG = 30.0f;
	MaxTurnRate = 50.0f;
	TerminalDeadbandRange = 100.0f;
	ProximityFuzeRadius = 600.0f;
	bEnableProximityFuze = true;
}

void UARMMissileGuidanceComponent::BeginPlay()
{
	Super::BeginPlay();

	FlightPhase = EARMFlightPhase::PreLaunch;
	bLoftComplete = false;
	TimeSinceEmissionLost = 0.0f;
	DeadReckoningElapsedTime = 0.0f;
}

void UARMMissileGuidanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UARMMissileGuidanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UARMMissileGuidanceComponent, FlightPhase, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UARMMissileGuidanceComponent, LastKnownEmitterLocation, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UARMMissileGuidanceComponent, PreBriefedTargetLocation, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UARMMissileGuidanceComponent, bHasPreBriefedTarget, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UARMMissileGuidanceComponent, bHasEmitterMemory, COND_OwnerOnly);
}

bool UARMMissileGuidanceComponent::PrepareLaunch(const FMissileLaunchConfiguration& Configuration)
{
	UAircraftRadarComponent* CuedEmitter = nullptr;
	if (IsValid(Configuration.Target.TargetActor))
	{
		CuedEmitter = Configuration.Target.TargetActor->FindComponentByClass<UAircraftRadarComponent>();
		if (!IsValid(CuedEmitter) || !CuedEmitter->IsRadarEmitting()) return false;
	}
	if (!Super::PrepareLaunch(Configuration)) return false;
	if (IsValid(CuedEmitter)) HandoffEmitter(Configuration.Target.TargetActor, CuedEmitter);
	else if (Configuration.Target.bValid && GuidanceMode == EARMGuidanceMode::PreBriefed)
		SetPreBriefedTargetLocation(Configuration.Target.Position);
	return true;
}

bool UARMMissileGuidanceComponent::CanFireWeapon() const
{
	if (!bIsWeaponActivated || bWeaponFired)
	{
		return false;
	}

	if (!bRequireLockToFire)
	{
		return true;
	}

	switch (GuidanceMode)
	{
		case EARMGuidanceMode::TargetOfOpportunity:
			return IsTrackingActiveEmission();

		case EARMGuidanceMode::PreBriefed:
			return bHasPreBriefedTarget || IsTrackingActiveEmission();

		case EARMGuidanceMode::SelfProtect:
			return IsTrackingActiveEmission() && HandoffThreatID != INDEX_NONE;

		default:
			return IsValid(GetLockedTarget());
	}
}

bool UARMMissileGuidanceComponent::FireWeapon()
{
	if (!CanFireWeapon())
	{
		return false;
	}

	const bool bLaunchSuccess = Super::FireWeapon();
	if (!bLaunchSuccess)
	{
		return false;
	}

	// Calculate initial distance to target or pre-briefed location
	const FVector MissilePos = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
	const FVector TargetPos = IsTrackingActiveEmission() && IsValid(GetLockedTarget())
		? GetLockedTarget()->GetActorLocation()
		: (bHasEmitterMemory ? LastKnownEmitterLocation : PreBriefedTargetLocation);

	const float InitialDist = FVector::Dist(MissilePos, TargetPos);

	// Select initial flight phase
	if (bEnableLoft && InitialDist >= LoftMinLaunchRange)
	{
		TransitionToPhase(EARMFlightPhase::Loft);
	}
	else if (IsTrackingActiveEmission())
	{
		TransitionToPhase(EARMFlightPhase::TerminalTracking);
	}
	else if (GuidanceMode == EARMGuidanceMode::PreBriefed)
	{
		TransitionToPhase(EARMFlightPhase::MidCourse);
	}
	else if (bHasEmitterMemory)
	{
		TransitionToPhase(EARMFlightPhase::DeadReckoning);
	}
	else
	{
		TransitionToPhase(EARMFlightPhase::TerminalTracking);
	}

	return true;
}

void UARMMissileGuidanceComponent::TransitionToPhase(EARMFlightPhase NewPhase)
{
	if (FlightPhase != NewPhase)
	{
		FlightPhase = NewPhase;
		OnPhaseChanged.Broadcast(NewPhase);
	}
}

bool UARMMissileGuidanceComponent::IsTrackingActiveEmission() const
{
	if (!IsValid(GetLockedTarget()))
	{
		return false;
	}

	if (TargetRadarComponent.IsValid())
	{
		return TargetRadarComponent->IsRadarEmitting();
	}

	if (UAircraftRadarComponent* Radar = GetLockedTarget()->FindComponentByClass<UAircraftRadarComponent>())
	{
		return Radar->IsRadarEmitting();
	}

	return false;
}

void UARMMissileGuidanceComponent::HandoffEmitter(AActor* InEmitterActor, UAircraftRadarComponent* InRadarComp)
{
	if (!IsValid(InEmitterActor))
	{
		TargetRadarComponent.Reset();
		LockMissile(nullptr);
		return;
	}

	UAircraftRadarComponent* Radar = InRadarComp;
	if (!Radar)
	{
		Radar = InEmitterActor->FindComponentByClass<UAircraftRadarComponent>();
	}

	TargetRadarComponent = Radar;
	LockMissile(InEmitterActor);

	LastKnownEmitterLocation = InEmitterActor->GetActorLocation();
	bHasEmitterMemory = true;
	LastKnownEmitterVelocity = InEmitterActor->GetVelocity();
	TimeSinceEmissionLost = 0.0f;
	DeadReckoningElapsedTime = 0.0f;

	OnEmitterAcquired.Broadcast(InEmitterActor, Radar);
}

bool UARMMissileGuidanceComponent::HandoffFromRWR(URadarWarningReceiverComponent* InRWR, int32 ThreatID)
{
	if (!InRWR)
	{
		return false;
	}

	const TArray<FRWRThreatEntry>& Threats = InRWR->GetAllThreats();
	for (const FRWRThreatEntry& Threat : Threats)
	{
		if (Threat.ThreatID == ThreatID)
		{
			return HandoffFromRWRThreat(Threat);
		}
	}

	return false;
}

bool UARMMissileGuidanceComponent::HandoffFromRWRThreat(const FRWRThreatEntry& ThreatEntry)
{
	AActor* ThreatActor = ThreatEntry.SourceActor.Get();
	if (!IsValid(ThreatActor))
	{
		return false;
	}

	UAircraftRadarComponent* RadarComp = ThreatActor->FindComponentByClass<UAircraftRadarComponent>();
	if (!IsValid(RadarComp) || !RadarComp->IsRadarEmitting()) return false;
	HandoffEmitter(ThreatActor, RadarComp);
	HandoffThreatID = ThreatEntry.ThreatID;

	OnRWRHandoffReceived.Broadcast(ThreatEntry.ThreatID, ThreatActor);
	return true;
}

void UARMMissileGuidanceComponent::SetPreBriefedTargetLocation(const FVector& InLocation)
{
	PreBriefedTargetLocation = InLocation;
	bHasPreBriefedTarget = true;
	bHasEmitterMemory = true;
	LastKnownEmitterLocation = InLocation;
	LastKnownEmitterVelocity = FVector::ZeroVector;
}

bool UARMMissileGuidanceComponent::IsRadarCandidateEligible(UAircraftRadarComponent* Candidate, float& OutAngleDeg, float& OutDistCm) const
{
	OutAngleDeg = 0.0f;
	OutDistCm = 0.0f;

	if (!Candidate || !Candidate->IsRadarEmitting())
	{
		return false;
	}

	AActor* CandidateOwner = Candidate->GetOwner();
	if (!IsValid(CandidateOwner) || !IsCandidateTargetEligible(CandidateOwner))
	{
		return false;
	}

	// Filter by target tags if specified
	if (TargetEmitterFilterTags.Num() > 0)
	{
		bool bTagMatched = false;
		for (const FName& FilterTag : TargetEmitterFilterTags)
		{
			if (CandidateOwner->ActorHasTag(FilterTag))
			{
				bTagMatched = true;
				break;
			}
		}
		if (!bTagMatched)
		{
			return false;
		}
	}

	FVector SeekerPos = FVector::ZeroVector;
	FRotator SeekerRot = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerPos, SeekerRot);

	const FVector EmitterPos = Candidate->GetRadarLocation();
	const FVector ToEmitter = EmitterPos - SeekerPos;
	OutDistCm = ToEmitter.Size();

	if (OutDistCm > PassiveSeekerMaxRange || OutDistCm < 100.0f)
	{
		return false;
	}

	const FVector DirToEmitter = ToEmitter / OutDistCm;
	const FVector SeekerFwd = SeekerRot.Vector();

	const float Dot = FVector::DotProduct(SeekerFwd, DirToEmitter);
	OutAngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));

	// Check against maximum gimbal limit and passive seeker cone
	if (OutAngleDeg > GimbalLimitAngle)
	{
		return false;
	}

	return true;
}

void UARMMissileGuidanceComponent::PerformPassiveSeekerScan()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector SeekerPos = FVector::ZeroVector;
	FRotator SeekerRot = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerPos, SeekerRot);

	TArray<UAircraftRadarComponent*> CandidateRadars;

	// Query from spatial combat subsystem if available
	if (UAircraftCombatSubsystem* Subsystem = UAircraftCombatSubsystem::Get(this))
	{
		Subsystem->GetRadarsInRange(SeekerPos, PassiveSeekerMaxRange, CandidateRadars);
	}
	else
	{
		// Fallback: scene sphere overlap query
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ARMSeekerScan), false, GetOwner());
		PopulateSeekerIgnoredActors(QueryParams);

		TArray<FOverlapResult> Overlaps;
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(FMath::Min(PassiveSeekerMaxRange, 2000000.0f));
		if (World->OverlapMultiByChannel(Overlaps, SeekerPos, FQuat::Identity, DetectionChannel, Sphere, QueryParams))
		{
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (AActor* Act = Overlap.GetActor())
				{
					if (UAircraftRadarComponent* Radar = Act->FindComponentByClass<UAircraftRadarComponent>())
					{
						CandidateRadars.AddUnique(Radar);
					}
				}
			}
		}
	}

	UAircraftRadarComponent* BestRadar = nullptr;
	float BestScore = -MAX_flt;

	for (UAircraftRadarComponent* Candidate : CandidateRadars)
	{
		float AngleDeg = 0.0f;
		float DistCm = 0.0f;
		if (!IsRadarCandidateEligible(Candidate, AngleDeg, DistCm))
		{
			continue;
		}

		// Scoring heuristic:
		// Radars inside PassiveSeekerConeAngle receive high weight.
		// Radars in STT lock-on mode receive substantial priority boost.
		// Closer targets score higher.
		float Score = 1000.0f - (DistCm / 10000.0f) - (AngleDeg * 10.0f);

		if (bPrioritizeLockOnEmitters)
		{
			if (Candidate->RadarMode == ERadarOperatingMode::SingleTargetTrack)
			{
				Score += 5000.0f;
			}
			else if (Candidate->RadarMode == ERadarOperatingMode::TrackWhileScan)
			{
				Score += 2000.0f;
			}
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestRadar = Candidate;
		}
	}

	if (BestRadar)
	{
		AActor* EmitterOwner = BestRadar->GetOwner();
		HandoffEmitter(EmitterOwner, BestRadar);

		if (bWeaponFired)
		{
			TransitionToPhase(EARMFlightPhase::TerminalTracking);
		}
	}
}

void UARMMissileGuidanceComponent::VerifyCurrentEmitterEmission(float DeltaTime)
{
	AActor* TargetActor = GetLockedTarget();
	if (!IsValid(TargetActor))
	{
		// Target actor was destroyed or invalidated
		TransitionToPhase(EARMFlightPhase::DeadReckoning);
		return;
	}

	// Verify radar component
	UAircraftRadarComponent* RadarComp = TargetRadarComponent.Get();
	if (!RadarComp)
	{
		RadarComp = TargetActor->FindComponentByClass<UAircraftRadarComponent>();
		TargetRadarComponent = RadarComp;
	}

	const bool bIsEmitting = RadarComp ? RadarComp->IsRadarEmitting() : false;

	if (bIsEmitting)
	{
		// Active radiation confirmed — update memory data
		TimeSinceEmissionLost = 0.0f;
		LastKnownEmitterLocation = TargetActor->GetActorLocation();
		LastKnownEmitterVelocity = TargetActor->GetVelocity();
	}
	else
	{
		// Target stopped radiating (or beam swept past grace threshold)
		TimeSinceEmissionLost += DeltaTime;
		if (TimeSinceEmissionLost >= EmissionLossGracePeriod)
		{
			// Enter dead reckoning memory mode
			TransitionToPhase(EARMFlightPhase::DeadReckoning);
			DeadReckoningElapsedTime = 0.0f;
			OnEmitterLost.Broadcast(TargetActor, LastKnownEmitterLocation);
		}
	}
}

FVector UARMMissileGuidanceComponent::ComputeLoftDirection(const FVector& ForwardDir, const FVector& ToTarget) const
{
	const FVector HorizonForward = FVector(ForwardDir.X, ForwardDir.Y, 0.0f).GetSafeNormal();
	if (HorizonForward.IsNearlyZero())
	{
		return ForwardDir;
	}

	const float LoftPitchRad = FMath::DegreesToRadians(LoftAngle);
	const FVector UpVector = FVector::UpVector;
	const FVector LoftVector = (HorizonForward * FMath::Cos(LoftPitchRad) + UpVector * FMath::Sin(LoftPitchRad)).GetSafeNormal();

	return LoftVector;
}

void UARMMissileGuidanceComponent::TickSeekerLogic(float DeltaTime)
{
	const FVector MissilePos = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
	const FVector TargetCoord = IsTrackingActiveEmission() && IsValid(GetLockedTarget())
		? GetLockedTarget()->GetActorLocation()
		: (bHasEmitterMemory ? LastKnownEmitterLocation : PreBriefedTargetLocation);

	const float DistToTarget = FVector::Dist(MissilePos, TargetCoord);

	if (!bWeaponFired)
	{
		// Pre-launch behavior: scan for targets or track cued emitter
		if (GuidanceMode == EARMGuidanceMode::TargetOfOpportunity)
		{
			if (!IsValid(GetLockedTarget()) || !IsTrackingActiveEmission())
			{
				PerformPassiveSeekerScan();
			}
		}
		return;
	}

	// In-flight state machine
	switch (FlightPhase)
	{
		case EARMFlightPhase::Loft:
		{
			// Check if we have climbed close enough to terminate the loft arc
			if (DistToTarget <= LoftTerminationRange)
			{
				bLoftComplete = true;
				if (IsTrackingActiveEmission())
				{
					TransitionToPhase(EARMFlightPhase::TerminalTracking);
				}
				else
				{
					TransitionToPhase(EARMFlightPhase::DeadReckoning);
				}
			}
			break;
		}

		case EARMFlightPhase::MidCourse:
		{
			// Pre-Briefed standoff cruise: continuously listen for target emitter transmission
			PerformPassiveSeekerScan();
			if (IsTrackingActiveEmission())
			{
				TransitionToPhase(EARMFlightPhase::TerminalTracking);
			}
			else if (DistToTarget <= LoftTerminationRange)
			{
				TransitionToPhase(EARMFlightPhase::DeadReckoning);
			}
			break;
		}

		case EARMFlightPhase::TerminalTracking:
		{
			// Live RF tracking — check if enemy shuts down transmitter
			VerifyCurrentEmitterEmission(DeltaTime);
			break;
		}

		case EARMFlightPhase::DeadReckoning:
		{
			DeadReckoningElapsedTime += DeltaTime;

			// Extrapolate position using last known emitter velocity
			LastKnownEmitterLocation += LastKnownEmitterVelocity * DeltaTime;

			// Reacquisition attempt if enemy radar resumes radiating
			if (bCanReacquireIfEmitterResumes)
			{
				// Check if original target resumed
				if (IsValid(GetLockedTarget()) && IsTrackingActiveEmission())
				{
					TransitionToPhase(EARMFlightPhase::TerminalTracking);
					OnEmitterReacquired.Broadcast(GetLockedTarget());
					break;
				}

				// Otherwise scan forward cone for active emitters
				PerformPassiveSeekerScan();
				if (IsTrackingActiveEmission())
				{
					TransitionToPhase(EARMFlightPhase::TerminalTracking);
					OnEmitterReacquired.Broadcast(GetLockedTarget());
					break;
				}
			}

			// Memory drift timeout
			if (DeadReckoningElapsedTime >= MemoryDriftTimeout)
			{
				TransitionToPhase(EARMFlightPhase::MemoryTimeout);
				OnDeadReckoningTimeout.Broadcast(LastKnownEmitterLocation);
			}
			break;
		}

		case EARMFlightPhase::MemoryTimeout:
		{
			// Continue ballistic flight toward final coordinates
			break;
		}

		default:
			break;
	}

	// Visual Debugging
	if (bEnableDebugTraces)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			FVector SeekerPos = FVector::ZeroVector;
			FRotator SeekerRot = FRotator::ZeroRotator;
			GetSeekerTransform(SeekerPos, SeekerRot);

			// Draw passive seeker FOV cone
			DrawDebugCone(
				World,
				SeekerPos,
				SeekerRot.Vector(),
				FMath::Min(PassiveSeekerMaxRange, 100000.0f),
				FMath::DegreesToRadians(PassiveSeekerConeAngle),
				FMath::DegreesToRadians(PassiveSeekerConeAngle),
				16,
				FlightPhase == EARMFlightPhase::TerminalTracking ? FColor::Yellow : FColor::Orange,
				false,
				-1.0f,
				0,
				1.5f
			);

			// Draw line to target
			if (FlightPhase == EARMFlightPhase::TerminalTracking && IsValid(GetLockedTarget()))
			{
				DrawDebugLine(World, SeekerPos, GetLockedTarget()->GetActorLocation(), FColor::Green, false, -1.0f, 0, 2.0f);
				DrawDebugSphere(World, GetLockedTarget()->GetActorLocation(), 200.0f, 12, FColor::Green, false, -1.0f, 0, 2.0f);
			}
			else if (bHasEmitterMemory)
			{
				DrawDebugLine(World, SeekerPos, LastKnownEmitterLocation, FColor::Red, false, -1.0f, 0, 1.5f);
				DrawDebugSphere(World, LastKnownEmitterLocation, 250.0f, 12, FColor::Red, false, -1.0f, 0, 2.0f);
			}
		}

		if (GEngine)
		{
			const UEnum* PhaseEnum = StaticEnum<EARMFlightPhase>();
			const FString PhaseName = PhaseEnum ? PhaseEnum->GetDisplayNameTextByValue(static_cast<int64>(FlightPhase)).ToString() : TEXT("Unknown");

			FString TargetName = TEXT("NONE");
			if (IsValid(GetLockedTarget()))
			{
				TargetName = GetLockedTarget()->GetName();
			}
			else if (bHasEmitterMemory)
			{
				TargetName = FString::Printf(TEXT("GPS-MEM (%.0f, %.0f)"), LastKnownEmitterLocation.X, LastKnownEmitterLocation.Y);
			}

			GEngine->AddOnScreenDebugMessage(
				FAircraftCombatDebug::Key_MissileBase + 10,
				0.0f,
				FlightPhase == EARMFlightPhase::TerminalTracking ? FColor::Green : FColor::Orange,
				FString::Printf(TEXT("[ARM SEAD] Phase: %s | Target: %s | Dist: %.1f km | MemTimer: %.1fs"),
					*PhaseName,
					*TargetName,
					DistToTarget / 100000.0f,
					DeadReckoningElapsedTime
				)
			);
		}
	}
}

bool UARMMissileGuidanceComponent::GetGuidanceTargetSolution(FMissileTargetSolution& OutSolution) const
{
	if (FlightPhase == EARMFlightPhase::MemoryTimeout) return false;
	OutSolution = FMissileTargetSolution();
	if (IsTrackingActiveEmission() && IsValid(GetLockedTarget()))
	{
		OutSolution.bValid = true;
		OutSolution.bMeasured = true;
		OutSolution.TargetActor = GetLockedTarget();
		OutSolution.Position = GetTargetTrackingLocation(GetLockedTarget());
		OutSolution.Velocity = GetLockedTarget()->GetVelocity();
		return true;
	}
	if (FlightPhase == EARMFlightPhase::DeadReckoning && bHasEmitterMemory)
	{
		OutSolution.bValid = true;
		OutSolution.Position = LastKnownEmitterLocation;
		return true;
	}
	if (bHasPreBriefedTarget)
	{
		OutSolution.bValid = true;
		OutSolution.Position = PreBriefedTargetLocation;
		return true;
	}
	if (bHasEmitterMemory)
	{
		OutSolution.bValid = true;
		OutSolution.Position = LastKnownEmitterLocation;
		return true;
	}
	return false;
}

void UARMMissileGuidanceComponent::UpdateGuidanceVelocity(float DeltaTime)
{
	Super::UpdateGuidanceVelocity(DeltaTime);
	if (FlightPhase != EARMFlightPhase::Loft || !bWeaponFired || bLoftComplete ||
		Velocity.IsNearlyZero()) return;
	FMissileTargetSolution Solution;
	if (!GetGuidanceTargetSolution(Solution)) return;
	const FVector Position = UpdatedComponent ? UpdatedComponent->GetComponentLocation() :
		(GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
	const FVector Forward = Velocity.GetSafeNormal();
	const FVector LoftDirection = ComputeLoftDirection(Forward,
		(Solution.Position - Position).GetSafeNormal());
	Velocity = FMath::VInterpNormalRotationTo(Forward, LoftDirection, DeltaTime,
		MaxTurnRate * 0.5f) * Velocity.Size();
	UpdateComponentVelocity();
}
