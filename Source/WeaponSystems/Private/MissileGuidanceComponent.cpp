// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "MissileGuidanceComponent.h"
#include "Weapon.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "AircraftCombatDebug.h"

UMissileGuidanceComponent::UMissileGuidanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = false;

	bRotationFollowsVelocity = true; // Auto-aligns actor rotation with flight path
	bShouldBounce = false;
	ProjectileGravityScale = 0.0f; // Disable default gravity
	InitialSpeed = 0.0f; // Kept at 0 so missile stays stationary until LaunchMissile() is explicitly called
	MaxSpeed = 35000.0f;
	Velocity = FVector::ZeroVector;

	SetIsReplicatedByDefault(true);
}

void UMissileGuidanceComponent::BeginPlay()
{
	Super::BeginPlay();

	// Ensure parent movement component max speed does not restrict cruise speed
	MaxSpeed = FMath::Max(MaxSpeed, MaxCruiseSpeed);

	Weapon = Cast<AWeapon>(GetOwner());
	if (!Weapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("UMissileGuidanceComponent: Owning actor is not an AWeapon instance. Owner: %s"), *GetNameSafe(GetOwner()));
	}
}

void UMissileGuidanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UMissileGuidanceComponent, LockedTarget, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMissileGuidanceComponent, TargetSolution, COND_OwnerOnly);
}

bool UMissileGuidanceComponent::PrepareLaunch(const FMissileLaunchConfiguration& Configuration)
{
	if (bWeaponFired || (GetOwner() && !GetOwner()->HasAuthority())) return false;
	if (IsValid(Configuration.Carrier)) InitializeWeapon(Configuration.Carrier);
	TargetSolution = Configuration.Target;
	if (IsValid(Configuration.Target.TargetActor)) LockMissile(Configuration.Target.TargetActor);
	return true;
}

void UMissileGuidanceComponent::ClearTargetSolution()
{
	if (bWeaponFired || (GetOwner() && !GetOwner()->HasAuthority())) return;
	TargetSolution = FMissileTargetSolution();
}

void UMissileGuidanceComponent::ActivateWeapon(bool bActivate)
{
	// Powers the weapon and enables/disables seeker tracking systems
	Super::ActivateWeapon(bActivate);

	if (!bIsWeaponActivated)
	{
		bFuzeTriggered = false;

		if (LockedTarget)
		{
			AActor* LostTarget = LockedTarget;
			LockedTarget = nullptr;
			OnTargetLockLost.Broadcast(LostTarget);
		}
	}
}

void UMissileGuidanceComponent::LockMissile(AActor* InTargetActor)
{
	if (LockedTarget != InTargetActor)
	{
		AActor* PrevTarget = LockedTarget;
		LockedTarget = InTargetActor;

		if (LockedTarget)
		{
			OnTargetLocked.Broadcast(LockedTarget);
		}
		else if (PrevTarget)
		{
			OnTargetLockLost.Broadcast(PrevTarget);
		}
	}
}

void UMissileGuidanceComponent::SetIgnoredActors(const TArray<AActor*>& InActors)
{
	AdditionalIgnoredActors.Empty(InActors.Num());
	AActor* MyOwner = GetOwner();
	UPrimitiveComponent* UpdatedPrim = Cast<UPrimitiveComponent>(UpdatedComponent);

	for (AActor* Actor : InActors)
	{
		if (IsValid(Actor))
		{
			AdditionalIgnoredActors.Add(Actor);

			if (MyOwner)
			{
				if (UpdatedPrim)
				{
					UpdatedPrim->IgnoreActorWhenMoving(Actor, true);
				}

				TInlineComponentArray<UPrimitiveComponent*, 16> OtherPrimitives(Actor);
				for (UPrimitiveComponent* OtherPrim : OtherPrimitives)
				{
					if (IsValid(OtherPrim))
					{
						OtherPrim->IgnoreActorWhenMoving(MyOwner, true);
						if (UpdatedPrim)
						{
							OtherPrim->IgnoreComponentWhenMoving(UpdatedPrim, true);
							UpdatedPrim->IgnoreComponentWhenMoving(OtherPrim, true);
						}
					}
				}
			}

			if (Weapon)
			{
				Weapon->SetupMutualCollisionIgnore(Actor, true);
			}
		}
	}
}

void UMissileGuidanceComponent::AddIgnoredActor(AActor* InActor)
{
	if (IsValid(InActor) && !AdditionalIgnoredActors.Contains(InActor))
	{
		AdditionalIgnoredActors.Add(InActor);

		if (AActor* MyOwner = GetOwner())
		{
			UPrimitiveComponent* UpdatedPrim = Cast<UPrimitiveComponent>(UpdatedComponent);
			if (UpdatedPrim)
			{
				UpdatedPrim->IgnoreActorWhenMoving(InActor, true);
			}

			TInlineComponentArray<UPrimitiveComponent*, 16> OtherPrimitives(InActor);
			for (UPrimitiveComponent* OtherPrim : OtherPrimitives)
			{
				if (IsValid(OtherPrim))
				{
					OtherPrim->IgnoreActorWhenMoving(MyOwner, true);
					if (UpdatedPrim)
					{
						OtherPrim->IgnoreComponentWhenMoving(UpdatedPrim, true);
						UpdatedPrim->IgnoreComponentWhenMoving(OtherPrim, true);
					}
				}
			}
		}

		if (Weapon)
		{
			Weapon->SetupMutualCollisionIgnore(InActor, true);
		}
	}
}

void UMissileGuidanceComponent::ClearIgnoredActors()
{
	AdditionalIgnoredActors.Empty();
}

TArray<AActor*> UMissileGuidanceComponent::GetIgnoredActors() const
{
	TArray<AActor*> Result;
	for (const TWeakObjectPtr<AActor>& WeakActor : AdditionalIgnoredActors)
	{
		if (WeakActor.IsValid())
		{
			Result.Add(WeakActor.Get());
		}
	}
	return Result;
}

bool UMissileGuidanceComponent::CanFireWeapon() const
{
	if (!bIsWeaponActivated || bWeaponFired) return false;
	if (!bRequireLockToFire) return true;
	return IsValid(LockedTarget) || TargetSolution.bValid;
}

bool UMissileGuidanceComponent::CanDetachWeapon() const
{
	return !bWeaponFired;
}

bool UMissileGuidanceComponent::FireWeapon()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return false;
	// Launch requires the weapon system to be activated and holding a valid target lock
	if (!CanFireWeapon())
	{
		return false;
	}

	// Detach weapon physically from aircraft and configure collision ignores
	if (!UpdatedComponent && GetOwner()) SetUpdatedComponent(GetOwner()->GetRootComponent());
	if (AActor* MissileActor = GetOwner())
	{
		if (MissileActor->GetAttachParentActor())
			MissileActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	if (Weapon)
	{
		Weapon->SetMounted(false, PlayerAircraft);
		if (IsValid(PlayerAircraft))
		{
			Weapon->SetupMutualCollisionIgnore(PlayerAircraft, true);
		}
	}
	else if (AActor* MyOwner = GetOwner())
	{
		if (IsValid(PlayerAircraft))
		{
			if (UPrimitiveComponent* UpdatedPrim = Cast<UPrimitiveComponent>(UpdatedComponent))
			{
				UpdatedPrim->IgnoreActorWhenMoving(PlayerAircraft, true);
				TInlineComponentArray<UPrimitiveComponent*, 16> AircraftPrimitives(PlayerAircraft);
				for (UPrimitiveComponent* AirPrim : AircraftPrimitives)
				{
					if (IsValid(AirPrim))
					{
						UpdatedPrim->IgnoreComponentWhenMoving(AirPrim, true);
						AirPrim->IgnoreComponentWhenMoving(UpdatedPrim, true);
						AirPrim->IgnoreActorWhenMoving(MyOwner, true);
					}
				}
			}
		}
	}

	LaunchRotation = UpdatedComponent ? UpdatedComponent->GetComponentRotation() : (GetOwner() ? GetOwner()->GetActorRotation() : FRotator::ZeroRotator);
	const FVector MissileForward = UpdatedComponent ? UpdatedComponent->GetForwardVector() : (GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector);

	// Inherit aircraft velocity based on configuration
	if (bInheritAircraftVelocity && IsValid(PlayerAircraft))
	{
		FVector AircraftVel = FVector::ZeroVector;
		const FVector StoreLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : (GetOwner() ? GetOwner()->GetActorLocation() : PlayerAircraft->GetActorLocation());

		if (const UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(PlayerAircraft->GetRootComponent()))
		{
			AircraftVel = RootPrim->IsSimulatingPhysics() ? RootPrim->GetPhysicsLinearVelocityAtPoint(StoreLocation) : RootPrim->GetPhysicsLinearVelocity();
			if (AircraftVel.IsNearlyZero())
			{
				AircraftVel = PlayerAircraft->GetVelocity();
			}
		}
		else
		{
			AircraftVel = PlayerAircraft->GetVelocity();
		}
		
		switch (InheritanceDirection)
		{
			case EVelocityInheritanceDirection::AircraftVelocityVector:
				// Apply true 3D velocity directly, scaled by the multiplier
				Velocity += AircraftVel * InheritedSpeedMultiplier;
				break;

			case EVelocityInheritanceDirection::AircraftForward:
				// Apply magnitude of speed along the aircraft's forward nose direction
				Velocity += PlayerAircraft->GetActorForwardVector() * (AircraftVel.Size() * InheritedSpeedMultiplier);
				break;

			case EVelocityInheritanceDirection::MissileForward:
			default:
				// Apply magnitude of speed along the missile's forward direction (rail direction)
				Velocity += MissileForward * (AircraftVel.Size() * InheritedSpeedMultiplier);
				break;
		}
	}
	
	// Add initial launch speed (preserves ejection impulse and inherited speed)
	Velocity += MissileForward * InitialSpeed;

	UpdateComponentVelocity();

	bWeaponFired = true;
	bFuzeTriggered = false;
	TimeSinceFired = 0.0f;
	OnWeaponFired.Broadcast(LockedTarget);
	return true;
}

void UMissileGuidanceComponent::UpdateGuidanceVelocity(float DeltaTime)
{
	TimeSinceFired += DeltaTime;
	MaxSpeed = FMath::Max(MaxSpeed, MaxCruiseSpeed);

	FVector CurrentVelocity = Velocity;
	float CurrentSpeed = CurrentVelocity.Size();

	// Determine reference forward direction: during ejection separation, preserve nose heading smoothly
	FVector ForwardDir;
	if (TimeSinceFired < MotorIgnitionDelay && bSmoothEjectionAttitude && UpdatedComponent)
	{
		ForwardDir = UpdatedComponent->GetForwardVector();

		// Smooth aerodynamic weathervaning during inert separation without instantaneous angular snapping
		const FRotator CurrentRot = UpdatedComponent->GetComponentRotation();
		const FRotator TargetRot = (CurrentSpeed > 100.0f) ? CurrentVelocity.Rotation() : LaunchRotation;
		const FRotator SmoothRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, EjectionAlignmentRate);
		UpdatedComponent->SetWorldRotation(SmoothRot);
		ForwardDir = SmoothRot.Vector();
	}
	else
	{
		ForwardDir = (CurrentSpeed > KINDA_SMALL_NUMBER)
			? (CurrentVelocity / CurrentSpeed)
			: (UpdatedComponent ? UpdatedComponent->GetForwardVector() : (GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector));
	}

	// 1. Longitudinal dynamics (motor acceleration profile)
	if (TimeSinceFired >= MotorIgnitionDelay)
	{
		if (bUseStagedMotorProfile)
		{
			const float BurnTime = TimeSinceFired - MotorIgnitionDelay;
			const float ThrustAcceleration = BurnTime <= BoostDurationSeconds ? MotorAcceleration :
				(BurnTime <= BoostDurationSeconds + SustainDurationSeconds ? SustainAcceleration : 0.0f);
			const float DragAcceleration = FMath::Max(0.0f, QuadraticDragCoefficient) * FMath::Square(CurrentSpeed);
			CurrentSpeed = FMath::Clamp(CurrentSpeed +
				(ThrustAcceleration - DragAcceleration) * DeltaTime, 0.0f, MaxCruiseSpeed);
		}
		else if (CurrentSpeed < MaxCruiseSpeed)
		{
			CurrentSpeed = FMath::Min(CurrentSpeed + (MotorAcceleration * DeltaTime), MaxCruiseSpeed);
		}
	}

	// 2. Lateral guidance (Proportional Navigation)
	FVector LateralAcceleration = FVector::ZeroVector;

	// The validity flag, rather than a nonzero position or actor pointer, permits coordinate-only shots.
	FMissileTargetSolution GuidanceTarget;
	if (TimeSinceFired >= GuidanceActivationDelay && CurrentSpeed > 100.0f &&
		GetGuidanceTargetSolution(GuidanceTarget))
	{
		const FVector MissileLocation = UpdatedComponent
			? UpdatedComponent->GetComponentLocation()
			: (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);

		const FVector R = GuidanceTarget.Position - MissileLocation;
		const float RangeSq = R.SizeSquared();
		const float Range = FMath::Sqrt(RangeSq);

		// Terminal deadband cutoff to avoid division-by-zero singularities near impact
		if (Range > TerminalDeadbandRange && RangeSq > KINDA_SMALL_NUMBER)
		{
			const FVector R_Hat = R / Range;
			const FVector V_Rel = GuidanceTarget.Velocity - CurrentVelocity;
			const float SpeedScale = FMath::Clamp(CurrentSpeed / FMath::Max(MaxCruiseSpeed, 1.0f), 0.0f, 1.0f);
			const float MaxAccelCm = MaxLateralG * 980.665f * SpeedScale;
			if (!GuidanceTarget.bMeasured)
			{
				// Pure PN cannot turn toward a static waypoint when LOS rate is zero.
				const FVector DesiredVelocity = R_Hat * CurrentSpeed;
				const FVector Requested = (DesiredVelocity - CurrentVelocity) / FMath::Max(DeltaTime, 0.001f);
				LateralAcceleration = FVector::VectorPlaneProject(Requested, ForwardDir).GetClampedToMaxSize(MaxAccelCm);
			}
			else
			{
				// Closing velocity (Vc = -dR/dt)
				const float Vc = -FVector::DotProduct(R_Hat, V_Rel);
				if (Vc > 0.0f)
				{
					// Line of Sight (LOS) angular rate: Omega = (R x V_Rel) / |R|^2
					const FVector Omega = FVector::CrossProduct(R, V_Rel) / RangeSq;

					// True Proportional Navigation (TPN): am = N * Vc * (Omega x R_Hat)
					LateralAcceleration = NavigationGain * Vc * FVector::CrossProduct(Omega, R_Hat);

					// Dynamic authority scaling by airspeed
					LateralAcceleration = LateralAcceleration.GetClampedToMaxSize(MaxAccelCm);
				}
			}
		}
	}

	// 3. Velocity recombination, turn-rate clamping & alignment
	FVector SteeredDirection = ForwardDir;
	if (!LateralAcceleration.IsNearlyZero())
	{
		const FVector RawSteeredVelocity = (ForwardDir * CurrentSpeed) + (LateralAcceleration * DeltaTime);
		const FVector DesiredDirection = RawSteeredVelocity.GetSafeNormal(KINDA_SMALL_NUMBER, ForwardDir);

		if (MaxTurnRate > 0.0f)
		{
			const float TurnRateScale = FMath::Clamp(CurrentSpeed / MaxCruiseSpeed, 0.1f, 1.0f);
			SteeredDirection = FMath::VInterpNormalRotationTo(ForwardDir, DesiredDirection, DeltaTime, MaxTurnRate * TurnRateScale);
		}
		else
		{
			SteeredDirection = DesiredDirection;
		}
	}

	Velocity = SteeredDirection * CurrentSpeed;
	UpdateComponentVelocity();

	// Align mesh orientation to flight path once motor accelerates or if smooth separation is not active
	if (bRotationFollowsVelocity && UpdatedComponent && !Velocity.IsNearlyZero())
	{
		if (TimeSinceFired >= MotorIgnitionDelay || !bSmoothEjectionAttitude)
		{
			UpdatedComponent->SetWorldRotation(Velocity.Rotation());
		}
	}
}

bool UMissileGuidanceComponent::GetGuidanceTargetSolution(FMissileTargetSolution& OutSolution) const
{
	if (IsValid(LockedTarget))
	{
		OutSolution = FMissileTargetSolution();
		OutSolution.bValid = true;
		OutSolution.bMeasured = true;
		OutSolution.TargetActor = LockedTarget;
		OutSolution.Position = GetTargetTrackingLocation(LockedTarget);
		OutSolution.Velocity = LockedTarget->GetVelocity();
		return true;
	}
	OutSolution = TargetSolution;
	if (!OutSolution.bValid) return false;
	if (OutSolution.bMeasured && GetWorld())
		OutSolution.Position += OutSolution.Velocity * FMath::Clamp(GetWorld()->GetTimeSeconds() - OutSolution.MeasurementTimeSeconds, 0.0f, 30.0f);
	return true;
}

void UMissileGuidanceComponent::GetSeekerTransform(FVector& OutLocation, FRotator& OutRotation) const
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;

	if (Weapon && Weapon->GetWeaponMesh() && Weapon->GetWeaponMesh()->DoesSocketExist(SeekerSocket))
	{
		OutLocation = Weapon->GetWeaponMesh()->GetSocketLocation(SeekerSocket);
		OutRotation = Weapon->GetWeaponMesh()->GetSocketRotation(SeekerSocket);
	}
	else if (UpdatedComponent)
	{
		OutLocation = UpdatedComponent->GetComponentLocation();
		OutRotation = UpdatedComponent->GetComponentRotation();
	}
	else if (AActor* OwnerActor = GetOwner())
	{
		OutLocation = OwnerActor->GetActorLocation();
		OutRotation = OwnerActor->GetActorRotation();
	}
}

FVector UMissileGuidanceComponent::GetTargetTrackingLocation(const AActor* InTarget) const
{
	if (!IsValid(InTarget))
	{
		return FVector::ZeroVector;
	}

	if (TargetTrackingSocket != NAME_None)
	{
		// Fast-path: Check target RootComponent first
		if (const USceneComponent* RootComp = InTarget->GetRootComponent())
		{
			if (RootComp->DoesSocketExist(TargetTrackingSocket))
			{
				return RootComp->GetSocketLocation(TargetTrackingSocket);
			}
		}

		// Fallback: Check other scene components with stack-allocated inline buffer
		TInlineComponentArray<USceneComponent*, 8> SceneComponents(InTarget);
		for (const USceneComponent* SceneComp : SceneComponents)
		{
			if (SceneComp && SceneComp->DoesSocketExist(TargetTrackingSocket))
			{
				return SceneComp->GetSocketLocation(TargetTrackingSocket);
			}
		}
	}

	return InTarget->GetActorLocation();
}

void UMissileGuidanceComponent::PopulateSeekerIgnoredActors(FCollisionQueryParams& OutParams) const
{
	AActor* OwnerActor = GetOwner();

	if (OwnerActor)
	{
		OutParams.AddIgnoredActor(OwnerActor);
	}

	if (Weapon)
	{
		OutParams.AddIgnoredActor(Weapon);
	}

	if (bIgnoreSelfAircraft)
	{
		if (IsValid(PlayerAircraft))
		{
			OutParams.AddIgnoredActor(PlayerAircraft);

			if (bIgnoreSelfAttachedActors)
			{
				TArray<AActor*> AttachedToPlayer;
				PlayerAircraft->GetAttachedActors(AttachedToPlayer, true, true);
				OutParams.AddIgnoredActors(AttachedToPlayer);
			}
		}

		if (bIgnoreSelfAttachedActors && OwnerActor)
		{
			TArray<AActor*> AttachedToOwner;
			OwnerActor->GetAttachedActors(AttachedToOwner, true, true);
			OutParams.AddIgnoredActors(AttachedToOwner);
		}
	}

	if (OwnerActor)
	{
		if (APawn* InstigatorPawn = OwnerActor->GetInstigator())
		{
			OutParams.AddIgnoredActor(InstigatorPawn);
		}
	}

	// Programmed ignored actors (parent aircraft and sibling stores configured by MMMS)
	for (const TWeakObjectPtr<AActor>& IgnoredPtr : AdditionalIgnoredActors)
	{
		if (IgnoredPtr.IsValid())
		{
			OutParams.AddIgnoredActor(IgnoredPtr.Get());
		}
	}
}

FVector UMissileGuidanceComponent::ComputeRotatedSeekerForward(const FRotator& SeekerRotation, const FVector2D& ConeRotation) const
{
	const FRotator OffsetRotator(ConeRotation.X, ConeRotation.Y, 0.0f);
	const FQuat CombinedQuat = SeekerRotation.Quaternion() * OffsetRotator.Quaternion();

	return CombinedQuat.GetForwardVector().GetSafeNormal();
}

bool UMissileGuidanceComponent::IsCandidateTargetEligible(const AActor* Candidate) const
{
	if (!IsValid(Candidate))
	{
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	if (Candidate == OwnerActor || Candidate == Weapon)
	{
		return false;
	}

	if (OwnerActor && Candidate == OwnerActor->GetInstigator())
	{
		return false;
	}

	// Explicit programmed ignore list check
	for (const TWeakObjectPtr<AActor>& IgnoredPtr : AdditionalIgnoredActors)
	{
		if (IgnoredPtr.IsValid() && IgnoredPtr.Get() == Candidate)
		{
			return false;
		}
	}

	if (bIgnoreSelfAircraft)
	{
		if (IsValid(PlayerAircraft))
		{
			if (Candidate == PlayerAircraft || Candidate->GetOwner() == PlayerAircraft)
			{
				return false;
			}

			if (bIgnoreSelfAttachedActors && (Candidate->IsAttachedTo(PlayerAircraft) || PlayerAircraft->IsAttachedTo(Candidate)))
			{
				return false;
			}
		}

		if (bIgnoreSelfAttachedActors && OwnerActor)
		{
			if (Candidate->IsAttachedTo(OwnerActor) || OwnerActor->IsAttachedTo(Candidate) || Candidate->GetOwner() == OwnerActor)
			{
				return false;
			}
		}
	}

	// IFF team-based friendly exclusion (IGenericTeamAgentInterface)
	if (bEnableIFF)
	{
		// Resolve the firing platform: prefer PlayerAircraft (the launching pawn), fall back to the weapon's owner actor
		const AActor* FiringPlatform = IsValid(PlayerAircraft) ? static_cast<const AActor*>(PlayerAircraft) : GetOwner();
		if (FiringPlatform)
		{
			const ETeamAttitude::Type FallbackAttitude = FCombatTeamUtility::UnknownAttitudeToTeamAttitude(UnknownTargetAttitude);
			const ETeamAttitude::Type Attitude = FCombatTeamUtility::GetAttitude(FiringPlatform, Candidate, FallbackAttitude);
			if (Attitude == ETeamAttitude::Friendly)
			{
				return false;
			}
		}
	}

	// Target filter tag filtering
	if (TargetFilterTags.Num() > 0)
	{
		bool bHasMatchingTag = false;
		for (const FName& Tag : TargetFilterTags)
		{
			if (Candidate->ActorHasTag(Tag))
			{
				bHasMatchingTag = true;
				break;
			}
		}

		if (!bHasMatchingTag)
		{
			return false;
		}
	}

	return true;
}

bool UMissileGuidanceComponent::CheckProximityFuze()
{
	if (!bEnableProximityFuze || bFuzeTriggered || ProximityFuzeRadius <= 0.0f)
	{
		return false;
	}

	// Safety: Proximity fuze only arms after weapon has launched and satisfied the arming delay
	if (!bWeaponFired || TimeSinceFired < FuzeArmingDelay)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);

	if (bEnableDebugTraces)
	{
		DrawDebugSphere(World, SeekerLocation, ProximityFuzeRadius, 16, FColor::Orange, false, -1.0f, 0, 1.0f);
	}

	FCollisionQueryParams FuzeQueryParams(SCENE_QUERY_STAT(MissileProximityFuzeOverlap), false, GetOwner());
	PopulateSeekerIgnoredActors(FuzeQueryParams);

	TArray<FOverlapResult> OverlapResults;
	const FCollisionShape SphereShape = FCollisionShape::MakeSphere(ProximityFuzeRadius);
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
			FuzeQueryParams
		);

		if (OverlapResults.IsEmpty())
		{
			bHasOverlaps = World->OverlapMultiByChannel(
				OverlapResults,
				SeekerLocation,
				FQuat::Identity,
				DetectionChannel,
				SphereShape,
				FuzeQueryParams
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
			FuzeQueryParams
		);
	}

	if (!bHasOverlaps || OverlapResults.IsEmpty())
	{
		return false;
	}

	TSet<AActor*> ProcessedActors;
	ProcessedActors.Reserve(OverlapResults.Num());

	for (const FOverlapResult& Overlap : OverlapResults)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || ProcessedActors.Contains(Candidate))
		{
			continue;
		}
		ProcessedActors.Add(Candidate);

		if (!IsFuzeTargetEligible(Candidate))
		{
			continue;
		}

		if (bFuzeOnlyTriggersOnLockedTarget && Candidate != LockedTarget)
		{
			continue;
		}

		// Line-of-sight raycast check: ensure fuze does not detonate through terrain or structures
		const FVector TargetLocation = GetTargetTrackingLocation(Candidate);
		FHitResult HitResult;
		const bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			SeekerLocation,
			TargetLocation,
			ECC_Visibility,
			FuzeQueryParams
		);

		const bool bLOSClear = !bHit || (HitResult.GetActor() == Candidate) || (HitResult.GetActor() && (HitResult.GetActor()->IsAttachedTo(Candidate) || Candidate->IsAttachedTo(HitResult.GetActor())));
		if (!bLOSClear)
		{
			continue;
		}

		// Proximity fuze detonated near candidate target (triggers exactly once per launch)
		bFuzeTriggered = true;

		if (bEnableDebugTraces)
		{
			DrawDebugSphere(World, Candidate->GetActorLocation(), 60.0f, 16, FColor::Red, false, 0.5f, 0, 2.0f);
		}

		OnProximityFuzeTriggered.Broadcast(Candidate);
		return true;
	}

	return false;
}

void UMissileGuidanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsWeaponActivated || !IsValid(this))
	{
		return;
	}

	const bool bHasAuthority = GetOwner() ? GetOwner()->HasAuthority() : true;

	// 1. Proximity fuze detection (Server Authoritative)
	if (bHasAuthority && bWeaponFired && !bFuzeTriggered && (IsValid(LockedTarget) || !bFuzeOnlyTriggersOnLockedTarget))
	{
		const bool bFuzeFired = CheckProximityFuze();
		if (bFuzeFired && (!IsValid(this) || !bIsWeaponActivated))
		{
			return;
		}
	}

	if (!bIsWeaponActivated || !IsValid(this))
	{
		return;
	}

	// Target designation visualizer
	if (bEnableDebugTraces && IsValid(LockedTarget))
	{
		if (UWorld* DebugWorld = GetWorld())
		{
			FVector SeekerLoc = FVector::ZeroVector;
			FRotator SeekerRot = FRotator::ZeroRotator;
			GetSeekerTransform(SeekerLoc, SeekerRot);

			const FVector LockedTargetLoc = GetTargetTrackingLocation(LockedTarget);

			FAircraftCombatDebug::DrawTargetDesignationBox(DebugWorld, LockedTargetLoc, LockedTarget->GetActorRotation(), FColor::Red, 80.0f);
			DrawDebugLine(DebugWorld, SeekerLoc, LockedTargetLoc, FColor::Red, false, -1.0f, 0, 1.5f);
		}
	}

	// Flight telemetry debug overlay
	if (bEnableDebugTraces && GEngine)
	{
		const float SpeedMps = Velocity.Size() / 100.0f;
		const float MaxSpeedMps = MaxCruiseSpeed / 100.0f;
		const float Mach = SpeedMps / 340.0f;

		FString MotorStatus;
		if (!bWeaponFired)
		{
			MotorStatus = TEXT("STANDBY (Not Launched)");
		}
		else if (TimeSinceFired < MotorIgnitionDelay)
		{
			MotorStatus = FString::Printf(TEXT("IGNITION DELAY (%.2fs / %.2fs)"), TimeSinceFired, MotorIgnitionDelay);
		}
		else if (Velocity.Size() < MaxCruiseSpeed)
		{
			MotorStatus = FString::Printf(TEXT("BURNING (+%.0f cm/s^2) -> %.0f m/s"), MotorAcceleration, MaxSpeedMps);
		}
		else
		{
			MotorStatus = FString::Printf(TEXT("CRUISE SPEED (%.0f m/s)"), MaxSpeedMps);
		}

		FString GuidanceStatus;
		if (!bWeaponFired)
		{
			GuidanceStatus = TEXT("CAGED / STANDBY");
		}
		else if (!IsValid(LockedTarget))
		{
			GuidanceStatus = TEXT("NO TARGET (Straight Flight)");
		}
		else if (TimeSinceFired < GuidanceActivationDelay)
		{
			GuidanceStatus = FString::Printf(TEXT("GUIDANCE DELAY (%.2fs / %.2fs)"), TimeSinceFired, GuidanceActivationDelay);
		}
		else
		{
			GuidanceStatus = FString::Printf(TEXT("PN TRACKING ACTIVE (Target: %s)"), *LockedTarget->GetName());
		}

		GEngine->AddOnScreenDebugMessage(FAircraftCombatDebug::Key_MissileBase + 0, 0.0f, FColor::Cyan, FString::Printf(TEXT("[MISSILE TELEMETRY] Flight Time: %.2fs | Launched: %s | Activated: %s"), TimeSinceFired, bWeaponFired ? TEXT("YES") : TEXT("NO"), bIsWeaponActivated ? TEXT("YES") : TEXT("NO")));
		GEngine->AddOnScreenDebugMessage(FAircraftCombatDebug::Key_MissileBase + 1, 0.0f, FColor::Yellow, FString::Printf(TEXT("[SPEED] Current: %.1f m/s (Mach %.2f) | Max Cruise: %.1f m/s"), SpeedMps, Mach, MaxSpeedMps));
		GEngine->AddOnScreenDebugMessage(FAircraftCombatDebug::Key_MissileBase + 2, 0.0f, FColor::Orange, FString::Printf(TEXT("[MOTOR] State: %s"), *MotorStatus));
		GEngine->AddOnScreenDebugMessage(FAircraftCombatDebug::Key_MissileBase + 3, 0.0f, FColor::Green, FString::Printf(TEXT("[GUIDANCE] State: %s"), *GuidanceStatus));
	}

	// 2. Subclass-specific seeker logic (IR homing, Radar homing, etc.)
	if (bHasAuthority || !bWeaponFired)
	{
		TickSeekerLogic(DeltaTime);
	}

	if (!IsValid(this) || !bIsWeaponActivated)
	{
		return;
	}

	// 3. Proportional navigation guidance flight (Server Authoritative)
	if (bHasAuthority && bWeaponFired && IsValid(this) && bIsWeaponActivated)
	{
		UpdateGuidanceVelocity(DeltaTime);
	}
}
