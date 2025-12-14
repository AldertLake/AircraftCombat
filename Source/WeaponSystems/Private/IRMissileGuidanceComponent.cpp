// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#include "IRMissileGuidanceComponent.h"
#include "Weapon.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "AircraftCombatSubsystem.h"

UIRMissileGuidanceComponent::UIRMissileGuidanceComponent()
{
	// IR missiles default to the Engine socket for heat tracking
	TargetTrackingSocket = FName(TEXT("Engine"));
	FiringRequirement = EWeaponFiringRequirement::Bugging;
	WeaponComponentType = EWeaponComponentType::IRMissile;
}

bool UIRMissileGuidanceComponent::PrepareLaunch(const FMissileLaunchConfiguration& Configuration)
{
	AActor* ExistingSeekerTarget = IsValid(GetLockedTarget()) ? GetLockedTarget() : nullptr;
	FMissileLaunchConfiguration SeekerConfiguration = Configuration;
	if (ExistingSeekerTarget != Configuration.Target.TargetActor)
		SeekerConfiguration.Target.TargetActor = nullptr;
	if (!Super::PrepareLaunch(SeekerConfiguration)) return false;
	if (IsValid(Configuration.Target.TargetActor))
	{
		if (ExistingSeekerTarget == Configuration.Target.TargetActor) return true;
		// The radar or SMS only cues the IR head; it cannot grant a seeker lock.
		LockMissile(nullptr);
		if (SlaveToTarget(Configuration.Target.TargetActor)) return true;
		SlaveToBoresight();
		return true;
	}
	if (IsValid(ExistingSeekerTarget)) LockMissile(ExistingSeekerTarget);
	else SlaveToBoresight();
	return true;
}

bool UIRMissileGuidanceComponent::CanFireWeapon(EWeaponLaunchFailureReason& OutReason) const
{
	if (!bIsWeaponActivated)
	{
		OutReason = EWeaponLaunchFailureReason::WeaponNotReady;
		return false;
	}
	if (bWeaponFired)
	{
		OutReason = EWeaponLaunchFailureReason::AmmoDepleted;
		return false;
	}
	if (FiringRequirement == EWeaponFiringRequirement::HardLock && !IsValid(GetLockedTarget()))
	{
		OutReason = EWeaponLaunchFailureReason::TargetLockRequired;
		return false;
	}
	OutReason = EWeaponLaunchFailureReason::None;
	return true;
}

void UIRMissileGuidanceComponent::TickSeekerLogic(float DeltaTime)
{
	if (GetLockedTarget() && !IsValid(GetLockedTarget())) LockMissile(nullptr);
	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);

	if (IsMissileLocked())
	{
		// Locked tracking mode: aim narrow seeker cone at target's heat source
		const FVector TargetLocation = GetTargetTrackingLocation(GetLockedTarget());
		const float DistanceToTarget = FVector::Dist(SeekerLocation, TargetLocation);
		if (DistanceToTarget > MaxSensorRange)
		{
			// Target exceeded maximum IR seeker sensor range — cannot maintain lock
			PreviousNarrowConeTargets.Empty();
			LockMissile(nullptr);
			return;
		}

		const FVector ToTarget = (TargetLocation - SeekerLocation).GetSafeNormal();
		const FVector LocalDir = SeekerRotation.UnrotateVector(ToTarget);
		const FRotator RelativeRot = LocalDir.Rotation();
		const FVector2D TargetConeRotation(RelativeRot.Pitch, RelativeRot.Yaw);

		CurrentConeRotation = FVector2D(
			FMath::Clamp(TargetConeRotation.X, MinConeRotation.X, MaxConeRotation.X),
			FMath::Clamp(TargetConeRotation.Y, MinConeRotation.Y, MaxConeRotation.Y)
		);

		TArray<AActor*> DetectedTargets;
		const bool bHasTargetInCone = FindTargetsInSeekerCone(
			MaxSensorRange,
			LockedModeConeAngle,
			TargetConeRotation,
			DetectionChannel,
			DetectedTargets
		);

		if (!bHasTargetInCone || !DetectedTargets.Contains(GetLockedTarget()))
		{
			// Target lost: broke gimbal limits, left narrow tracking cone, or obstructed by terrain/geometry
			PreviousNarrowConeTargets.Empty();
			LockMissile(nullptr);
		}
		else
		{
			// Target is still in tracking cone. Check if any new heat source (flares or crossing aircraft) entered
			if (TargetDistractionChance > 0.0f && DetectedTargets.Num() > 1)
			{
				// Check if there is any newly entered distractor (not present in cone during previous tick)
				bool bHasNewDistractor = false;
				for (AActor* Candidate : DetectedTargets)
				{
					if (IsValid(Candidate) && Candidate != GetLockedTarget())
					{
						if (!PreviousNarrowConeTargets.Contains(Candidate))
						{
							bHasNewDistractor = true;
							break;
						}
					}
				}

				if (bHasNewDistractor && FMath::FRand() < TargetDistractionChance)
				{
					// Seeker seduction: target closest distractor or flare
					AActor* ClosestDistractor = nullptr;
					float MinDistanceSq = TNumericLimits<float>::Max();

					for (AActor* Candidate : DetectedTargets)
					{
						if (IsValid(Candidate) && Candidate != GetLockedTarget())
						{
							const FVector CandidateLocation = GetTargetTrackingLocation(Candidate);
							const float DistSq = FVector::DistSquared(SeekerLocation, CandidateLocation);
							if (DistSq < MinDistanceSq)
							{
								MinDistanceSq = DistSq;
								ClosestDistractor = Candidate;
							}
						}
					}

					if (ClosestDistractor)
					{
						AActor* OldTarget = GetLockedTarget();
						LockMissile(ClosestDistractor);
						OnHeatSeekerChangeTarget.Broadcast(ClosestDistractor, OldTarget);
						if (!IsValid(this) || !IsWeaponActivated()) return;

					}
				}
			}

			// Update cache of targets in narrow cone
			PreviousNarrowConeTargets.Empty(DetectedTargets.Num());
			for (AActor* TargetInCone : DetectedTargets)
			{
				if (IsValid(TargetInCone))
				{
					PreviousNarrowConeTargets.Add(TargetInCone);
				}
			}
		}
	}
	else
	{
		// Search / acquisition mode: wide seeker cone facing forward or slaved to commanded location/direction
		PreviousNarrowConeTargets.Empty();

		FVector2D SearchConeRotation = FVector2D::ZeroVector;
		if (bIsSlavedToLocation)
		{
			if (AActor* Target = SlavedTarget.Get()) SlavedWorldLocation = GetTargetTrackingLocation(Target);
			else if (SlavedTarget.IsStale())
			{
				SlavedTarget.Reset();
				bIsSlavedToLocation = false;
				bIsSlavedToDirection = false;
				TransitionSeekerState(EWeaponSeekerState::Lost);
			}
			const FVector ToSlavedPoint = !bIsSlavedToLocation ? FVector::ZeroVector :
				(bIsSlavedToDirection ? SlavedWorldDirection : (SlavedWorldLocation - SeekerLocation).GetSafeNormal());
			if (!ToSlavedPoint.IsNearlyZero())
			{
				const FVector LocalDir = SeekerRotation.UnrotateVector(ToSlavedPoint);
				const FRotator RelativeRot = LocalDir.Rotation();
				SearchConeRotation = FVector2D(
					FMath::Clamp(RelativeRot.Pitch, MinConeRotation.X, MaxConeRotation.X),
					FMath::Clamp(RelativeRot.Yaw, MinConeRotation.Y, MaxConeRotation.Y)
				);
			}
		}

		CurrentConeRotation = SearchConeRotation;

		TArray<AActor*> DetectedTargets;
		const bool bHasTargetInCone = FindTargetsInSeekerCone(
			MaxSensorRange,
			SearchModeConeAngle,
			SearchConeRotation,
			DetectionChannel,
			DetectedTargets
		);

		if (bHasTargetInCone && DetectedTargets.Num() > 0)
		{
			// Find and acquire the target closest to the missile / slaved cone center
			AActor* ClosestTarget = SlavedTarget.IsValid() && DetectedTargets.Contains(SlavedTarget.Get()) ? SlavedTarget.Get() : nullptr;
			float MinDistanceSq = TNumericLimits<float>::Max();

			for (AActor* Candidate : DetectedTargets)
			{
				if (IsValid(Candidate) && !ClosestTarget)
				{
					const FVector CandidateLocation = GetTargetTrackingLocation(Candidate);
					const float DistSq = FVector::DistSquared(SeekerLocation, CandidateLocation);
					if (DistSq < MinDistanceSq)
					{
						MinDistanceSq = DistSq;
						ClosestTarget = Candidate;
					}
				}
			}

			if (ClosestTarget && GetLockedTarget() != ClosestTarget)
			{
				// Keep the external command so a later lock loss resumes cued acquisition.
				LockMissile(ClosestTarget);
			}
		}
	}
}

bool UIRMissileGuidanceComponent::SlaveToTarget(AActor* InTarget)
{
	if (!IsValid(InTarget)) return false;

	if (!IsCandidateTargetEligible(InTarget))
	{
		return false;
	}

	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);

	const FVector TargetLocation = GetTargetTrackingLocation(InTarget);
	const float Distance = FVector::Dist(SeekerLocation, TargetLocation);

	// If the selected target is beyond MaxSensorRange, the IR head cannot lock or be slaved to it
	if (Distance > MaxSensorRange) return false;

	// Check gimbal limits
	const FVector ToTarget = (TargetLocation - SeekerLocation).GetSafeNormal();
	const FVector LocalDir = SeekerRotation.UnrotateVector(ToTarget);
	const FRotator RelativeRot = LocalDir.Rotation();
	const FVector2D TargetConeRotation(RelativeRot.Pitch, RelativeRot.Yaw);

	if (TargetConeRotation.X < MinConeRotation.X || TargetConeRotation.X > MaxConeRotation.X ||
		TargetConeRotation.Y < MinConeRotation.Y || TargetConeRotation.Y > MaxConeRotation.Y)
	{
		return false;
	}

	// Verify line-of-sight
	if (UWorld* World = GetWorld())
	{
		FCollisionQueryParams SeekerQueryParams(SCENE_QUERY_STAT(MissileSeekerSlaveLOS), false, GetOwner());
		PopulateSeekerIgnoredActors(SeekerQueryParams);

		FHitResult HitResult;
		const bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			SeekerLocation,
			TargetLocation,
			ECC_Visibility,
			SeekerQueryParams
		);

		const bool bLOSClear = !bHit || (HitResult.GetActor() == InTarget) ||
			(HitResult.GetActor() && (HitResult.GetActor()->IsAttachedTo(InTarget) || InTarget->IsAttachedTo(HitResult.GetActor())));

		if (!bLOSClear)
		{
			return false;
		}
	}

	SlavedTarget = InTarget;
	bIsSlavedToLocation = true;
	bIsSlavedToDirection = false;
	SlavedWorldLocation = TargetLocation;
	bSeekerCaged = false;
	if (GetLockedTarget() == InTarget)
	{
		TransitionSeekerState(bIsWeaponActivated ? EWeaponSeekerState::Tracking : EWeaponSeekerState::Standby,
			bIsWeaponActivated ? InTarget : nullptr);
		return true;
	}
	LockMissile(nullptr);
	CurrentConeRotation = FVector2D(
		FMath::Clamp(TargetConeRotation.X, MinConeRotation.X, MaxConeRotation.X),
		FMath::Clamp(TargetConeRotation.Y, MinConeRotation.Y, MaxConeRotation.Y)
	);
	TransitionSeekerState(bIsWeaponActivated ? EWeaponSeekerState::Slaved : EWeaponSeekerState::Standby);
	return true;
}

bool UIRMissileGuidanceComponent::SlaveToLocation(const FVector& InWorldLocation)
{
	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);

	const FVector ToLocation = (InWorldLocation - SeekerLocation).GetSafeNormal();
	if (ToLocation.IsNearlyZero())
	{
		return false;
	}

	const FVector LocalDir = SeekerRotation.UnrotateVector(ToLocation);
	const FRotator RelativeRot = LocalDir.Rotation();

	const bool bWithinGimbalLimits = (RelativeRot.Pitch >= MinConeRotation.X && RelativeRot.Pitch <= MaxConeRotation.X &&
	                                  RelativeRot.Yaw >= MinConeRotation.Y && RelativeRot.Yaw <= MaxConeRotation.Y);
	if (!bWithinGimbalLimits) return false;
	SlavedTarget.Reset();
	SlavedWorldLocation = InWorldLocation;
	bIsSlavedToLocation = true;
	bIsSlavedToDirection = false;
	bSeekerCaged = false;
	LockMissile(nullptr);

	CurrentConeRotation = FVector2D(
		FMath::Clamp(RelativeRot.Pitch, MinConeRotation.X, MaxConeRotation.X),
		FMath::Clamp(RelativeRot.Yaw, MinConeRotation.Y, MaxConeRotation.Y)
	);

	TransitionSeekerState(bIsWeaponActivated ? EWeaponSeekerState::Slaved : EWeaponSeekerState::Standby);
	return true;
}

bool UIRMissileGuidanceComponent::SlaveToDirection(const FVector& InWorldDirection)
{
	if (InWorldDirection.IsNearlyZero())
	{
		return false;
	}

	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);

	const FVector NormalizedDir = InWorldDirection.GetSafeNormal();
	const FVector LocalDir = SeekerRotation.UnrotateVector(NormalizedDir);
	const FRotator RelativeRot = LocalDir.Rotation();

	const bool bWithinGimbalLimits = (RelativeRot.Pitch >= MinConeRotation.X && RelativeRot.Pitch <= MaxConeRotation.X &&
	                                  RelativeRot.Yaw >= MinConeRotation.Y && RelativeRot.Yaw <= MaxConeRotation.Y);
	if (!bWithinGimbalLimits) return false;
	SlavedTarget.Reset();
	SlavedWorldLocation = SeekerLocation + (NormalizedDir * MaxSensorRange);
	bIsSlavedToLocation = true;
	bIsSlavedToDirection = true;
	SlavedWorldDirection = NormalizedDir;
	bSeekerCaged = false;
	LockMissile(nullptr);

	CurrentConeRotation = FVector2D(
		FMath::Clamp(RelativeRot.Pitch, MinConeRotation.X, MaxConeRotation.X),
		FMath::Clamp(RelativeRot.Yaw, MinConeRotation.Y, MaxConeRotation.Y)
	);

	TransitionSeekerState(bIsWeaponActivated ? EWeaponSeekerState::Slaved : EWeaponSeekerState::Standby);
	return true;
}

void UIRMissileGuidanceComponent::SlaveToBoresight()
{
	SlavedTarget.Reset();
	bIsSlavedToLocation = false;
	bIsSlavedToDirection = false;
	SlavedWorldLocation = FVector::ZeroVector;
	SlavedWorldDirection = FVector::ForwardVector;
	CurrentConeRotation = FVector2D::ZeroVector;
	bSeekerCaged = true;
	LockMissile(nullptr);
	TransitionSeekerState(bIsWeaponActivated ? EWeaponSeekerState::Caged : EWeaponSeekerState::Standby);
}

void UIRMissileGuidanceComponent::SetSeekerCaged(bool bCaged)
{
	if (bCaged) { SlaveToBoresight(); return; }
	bSeekerCaged = false;
	if (!bIsWeaponActivated) return;
	if (IsValid(GetLockedTarget())) TransitionSeekerState(EWeaponSeekerState::Tracking, GetLockedTarget());
	else TransitionSeekerState(bIsSlavedToLocation ? EWeaponSeekerState::Slaved : EWeaponSeekerState::Lost);
}

void UIRMissileGuidanceComponent::LockMissile(AActor* InTargetActor)
{
	Super::LockMissile(InTargetActor);
	if (IsValid(InTargetActor))
	{
		bSeekerCaged = false;
		if (bIsWeaponActivated) TransitionSeekerState(EWeaponSeekerState::Tracking, InTargetActor);
	}
	else if (bIsWeaponActivated)
		TransitionSeekerState(bIsSlavedToLocation ? EWeaponSeekerState::Slaved :
			(bSeekerCaged ? EWeaponSeekerState::Caged : EWeaponSeekerState::Lost));
}

void UIRMissileGuidanceComponent::ActivateWeapon(bool bActivate)
{
	Super::ActivateWeapon(bActivate);
	if (bActivate)
	{
		if (IsValid(GetLockedTarget())) TransitionSeekerState(EWeaponSeekerState::Tracking, GetLockedTarget());
		else TransitionSeekerState(bIsSlavedToLocation ? EWeaponSeekerState::Slaved :
			(bSeekerCaged ? EWeaponSeekerState::Caged : EWeaponSeekerState::Lost));
	}
}

FVector UIRMissileGuidanceComponent::GetSeekerLookDirection() const
{
	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);

	return ComputeRotatedSeekerForward(SeekerRotation, CurrentConeRotation);
}

float UIRMissileGuidanceComponent::GetSeekerGimbalLimitAngle() const
{
	return FMath::Max(FMath::Max(FMath::Abs(MinConeRotation.X), FMath::Abs(MaxConeRotation.X)),
		FMath::Max(FMath::Abs(MinConeRotation.Y), FMath::Abs(MaxConeRotation.Y)));
}

EWeaponAudioTone UIRMissileGuidanceComponent::GetSeekerAudioTone() const
{
	if (!bIsWeaponActivated) return EWeaponAudioTone::Silent;
	if (IsValid(GetLockedTarget())) return EWeaponAudioTone::Locked;
	return bIsSlavedToLocation ? EWeaponAudioTone::Searching : EWeaponAudioTone::Silent;
}

float UIRMissileGuidanceComponent::GetSeekerSignalStrength() const
{
	if (!IsMissileLocked() || !IsValid(GetOwner()) || MaxSensorRange <= 0.0f) return 0.0f;
	const float Range = FVector::Dist(GetOwner()->GetActorLocation(), GetTargetTrackingLocation(GetLockedTarget()));
	return FMath::Clamp(1.0f - Range / MaxSensorRange, 0.0f, 1.0f);
}

bool UIRMissileGuidanceComponent::FindTargetsInSeekerCone(
	float MaxRange,
	float ConeHalfAngleDeg,
	FVector2D ConeRotation,
	ECollisionChannel OverlapChannel,
	TArray<AActor*>& OutTargets)
{
	OutTargets.Reset();

	UWorld* World = GetWorld();
	if (!World || MaxRange <= 0.0f || ConeHalfAngleDeg <= 0.0f)
	{
		return false;
	}

	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);

	// Clamp cone rotation within gimbal limits
	const float ClampedPitch = FMath::Clamp(ConeRotation.X, MinConeRotation.X, MaxConeRotation.X);
	const float ClampedYaw = FMath::Clamp(ConeRotation.Y, MinConeRotation.Y, MaxConeRotation.Y);
	const FVector2D ClampedConeRotation(ClampedPitch, ClampedYaw);

	const FVector SeekerForward = ComputeRotatedSeekerForward(SeekerRotation, ClampedConeRotation);
	if (SeekerForward.IsNearlyZero())
	{
		return false;
	}

	const float ConeHalfAngleRad = FMath::DegreesToRadians(ConeHalfAngleDeg);
	const float CosConeHalfAngle = FMath::Cos(ConeHalfAngleRad);
	const float MaxRangeSq = FMath::Square(MaxRange);

	// Render Seeker Cone Debug Visualizer
	if (bEnableDebugTraces)
	{
		DrawDebugCone(
			World,
			SeekerLocation,
			SeekerForward,
			FMath::Min(MaxRange, 50000.0f) * 0.25f,
			ConeHalfAngleRad,
			ConeHalfAngleRad,
			16,
			FColor::Yellow,
			false,
			-1.0f,
			0,
			1.0f
		);
	}

	// Stage 1: Candidate discovery query
	FCollisionQueryParams SeekerQueryParams(SCENE_QUERY_STAT(MissileSeekerQuery), false, GetOwner());
	PopulateSeekerIgnoredActors(SeekerQueryParams);

	TArray<AActor*> Candidates;
	if (UAircraftCombatSubsystem* CombatSubsystem = World->GetSubsystem<UAircraftCombatSubsystem>())
	{
		TArray<APawn*> Pawns;
		CombatSubsystem->GetCombatPawnsInRange(SeekerLocation, MaxRange, Pawns);
		for (APawn* P : Pawns)
		{
			if (P)
			{
				Candidates.Add(P);
			}
		}
	}

	{
		TArray<FOverlapResult> OverlapResults;
		const FCollisionShape SphereShape = FCollisionShape::MakeSphere(MaxRange);
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
				SeekerQueryParams
			);

			if (OverlapResults.IsEmpty())
			{
				bHasOverlaps = World->OverlapMultiByChannel(
					OverlapResults,
					SeekerLocation,
					FQuat::Identity,
					OverlapChannel,
					SphereShape,
					SeekerQueryParams
				);
			}
		}
		else
		{
			bHasOverlaps = World->OverlapMultiByChannel(
				OverlapResults,
				SeekerLocation,
				FQuat::Identity,
				OverlapChannel,
				SphereShape,
				SeekerQueryParams
			);
		}

		if (bHasOverlaps)
		{
			for (const FOverlapResult& Overlap : OverlapResults)
			{
				if (AActor* Candidate = Overlap.GetActor())
				{
					Candidates.AddUnique(Candidate);
				}
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		return false;
	}

	// Stage 2 & 3: Candidate evaluation, cone filtering, and LOS raycasts
	TSet<AActor*> ProcessedActors;
	ProcessedActors.Reserve(Candidates.Num());

	for (AActor* Candidate : Candidates)
	{
		if (!Candidate || ProcessedActors.Contains(Candidate))
		{
			continue;
		}
		ProcessedActors.Add(Candidate);

		// Target eligibility filter
		if (!IsCandidateTargetEligible(Candidate))
		{
			continue;
		}

		// Distance and cone angle check
		const FVector TargetLocation = GetTargetTrackingLocation(Candidate);
		const FVector ToTarget = TargetLocation - SeekerLocation;
		const float DistanceSq = ToTarget.SizeSquared();

		if (DistanceSq <= KINDA_SMALL_NUMBER || DistanceSq > MaxRangeSq)
		{
			continue;
		}

		const FVector ToTargetDirection = ToTarget.GetSafeNormal();
		const float DotProduct = FVector::DotProduct(SeekerForward, ToTargetDirection);

		if (DotProduct < CosConeHalfAngle)
		{
			continue;
		}

		// Line-of-sight check
		FHitResult HitResult;
		const bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			SeekerLocation,
			TargetLocation,
			ECC_Visibility,
			SeekerQueryParams
		);

		const bool bLOSClear = !bHit || (HitResult.GetActor() == Candidate) || (HitResult.GetActor() && (HitResult.GetActor()->IsAttachedTo(Candidate) || Candidate->IsAttachedTo(HitResult.GetActor())));

		if (bEnableDebugTraces)
		{
			const bool bIsActiveLock = (Candidate == GetLockedTarget());
			const FVector TraceEnd = bHit ? HitResult.ImpactPoint : TargetLocation;
			const FColor TraceColor = bLOSClear ? (bIsActiveLock ? FColor::Red : FColor::Green) : FColor::Red;
			DrawDebugLine(World, SeekerLocation, TraceEnd, TraceColor, false, -1.0f, 0, 1.5f);

			if (bHit && !bLOSClear)
			{
				DrawDebugPoint(World, HitResult.ImpactPoint, 8.0f, FColor::Red, false, -1.0f);
			}
			else if (bLOSClear)
			{
				if (bIsActiveLock)
				{
					DrawDebugSphere(World, TargetLocation, 60.0f, 16, FColor::Red, false, -1.0f, 0, 2.0f);
				}
				else
				{
					DrawDebugSphere(World, TargetLocation, 40.0f, 8, FColor::Green, false, -1.0f, 0, 1.5f);
				}
			}
		}

		if (bLOSClear)
		{
			OutTargets.Add(Candidate);
		}
	}

	return OutTargets.Num() > 0;
}
