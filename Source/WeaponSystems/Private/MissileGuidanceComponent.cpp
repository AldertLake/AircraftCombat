// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
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

	// Allow movement component speed to reach cruise speed
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
	if (bWeaponFired) return false;
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && GetOwner() && !GetOwner()->HasAuthority()) return false;
	if (IsValid(Configuration.Carrier)) InitializeWeapon(Configuration.Carrier);
	TargetSolution = Configuration.Target;
	LockMissile(IsValid(Configuration.Target.TargetActor) ? Configuration.Target.TargetActor.Get() : nullptr);
	return true;
}

void UMissileGuidanceComponent::ClearTargetSolution()
{
	if (bWeaponFired) return;
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && GetOwner() && !GetOwner()->HasAuthority()) return;
	TargetSolution = FMissileTargetSolution();
}

void UMissileGuidanceComponent::ActivateWeapon(bool bActivate)
{
	// Powers the weapon and enables/disables seeker tracking systems
	const bool bWasActivated = bIsWeaponActivated;
	Super::ActivateWeapon(bActivate);
	if (!bActivate) TransitionSeekerState(EWeaponSeekerState::Standby);
	else if (!bWasActivated) TransitionSeekerState(EWeaponSeekerState::Caged);

	if (!bIsWeaponActivated)
	{
		// Power changes or a fuze callback must not rearm an already launched missile.
		if (!bWeaponFired) bFuzeTriggered = false;
		bHasPreviousFuzeSample = false;

		LockedTarget = nullptr;
	}
}

bool UMissileGuidanceComponent::GetDynamicLaunchZone(const AActor* Target,
	float& OutRmin, float& OutRne, float& OutRmax) const
{
	OutRmin = OutRne = OutRmax = 0.0f;
	if (!IsValid(Target) || !IsValid(GetOwner()) || MaxCruiseSpeed <= 0.0f ||
		EffectiveFlightTimeSeconds <= 0.0f) return false;
	const FVector Source = GetOwner()->GetActorLocation();
	const FVector ToTarget = Target->GetActorLocation() - Source;
	const float Range = ToTarget.Size();
	if (Range <= KINDA_SMALL_NUMBER) return false;
	const FVector LOS = ToTarget / Range;
	const FVector CarrierVelocity = IsValid(PlayerAircraft) ? PlayerAircraft->GetVelocity() : GetOwner()->GetVelocity();
	const float RelativeClosure = FVector::DotProduct(CarrierVelocity - Target->GetVelocity(), LOS);
	const float SpeedAtLaunch = FMath::Max(InitialSpeed, FVector::DotProduct(Velocity, LOS));
	const float Horizon = FMath::Min(EffectiveFlightTimeSeconds, 180.0f);
	float Speed = FMath::Clamp(SpeedAtLaunch, 0.0f, MaxCruiseSpeed);
	float Reach = 0.0f;
	for (float Time = 0.0f; Time < Horizon; Time += 0.5f)
	{
		const float Step = FMath::Min(0.5f, Horizon - Time);
		if (Time >= MotorIgnitionDelay)
		{
			const float BurnTime = Time - MotorIgnitionDelay;
			const float Thrust = !bUseStagedMotorProfile ? MotorAcceleration :
				(BurnTime <= BoostDurationSeconds ? MotorAcceleration :
					(BurnTime <= BoostDurationSeconds + SustainDurationSeconds ? SustainAcceleration : 0.0f));
			const float Drag = bUseStagedMotorProfile ?
				FMath::Max(0.0f, QuadraticDragCoefficient) * FMath::Square(Speed) : 0.0f;
			Speed = FMath::Clamp(Speed + (Thrust - Drag) * Step, 0.0f, MaxCruiseSpeed);
		}
		Reach += Speed * Step;
	}
	const float AltitudeFactor = FMath::Clamp(1.0f + (Source.Z / 1000000.0f) * 0.15f, 0.8f, 1.3f);
	OutRmin = FMath::Max(TerminalDeadbandRange, FMath::Max(GuidanceActivationDelay, FuzeArmingDelay) *
		FMath::Max(SpeedAtLaunch, MaxCruiseSpeed * 0.25f));
	OutRmax = FMath::Max(OutRmin, (Reach * AltitudeFactor + RelativeClosure * Horizon) * 0.65f);
	OutRne = FMath::Clamp(OutRmax * 0.45f, OutRmin, OutRmax);
	return OutRmax > OutRmin;
}

float UMissileGuidanceComponent::GetEstimatedTimeToImpact(const AActor* Target) const
{
	if (!IsValid(Target) || !IsValid(GetOwner())) return 0.0f;
	const FVector ToTarget = Target->GetActorLocation() - GetOwner()->GetActorLocation();
	const float Range = ToTarget.Size();
	if (Range <= KINDA_SMALL_NUMBER) return 0.0f;
	const FVector MissileVelocity = bWeaponFired ? Velocity :
		(ToTarget / Range) * MaxCruiseSpeed + (IsValid(PlayerAircraft) ? PlayerAircraft->GetVelocity() : FVector::ZeroVector);
	const float ClosingSpeed = FVector::DotProduct(MissileVelocity - Target->GetVelocity(), ToTarget / Range);
	return ClosingSpeed > 100.0f ? Range / ClosingSpeed : 0.0f;
}

void UMissileGuidanceComponent::LockMissile(AActor* InTargetActor)
{
	LockedTarget = IsValid(InTargetActor) ? InTargetActor : nullptr;
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

bool UMissileGuidanceComponent::CanFireWeapon(EWeaponLaunchFailureReason& OutReason) const
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
	if (FiringRequirement == EWeaponFiringRequirement::HardLock && !IsValid(LockedTarget))
	{
		OutReason = EWeaponLaunchFailureReason::TargetLockRequired;
		return false;
	}
	OutReason = EWeaponLaunchFailureReason::None;
	return true;
}

bool UMissileGuidanceComponent::CanDetachWeapon() const
{
	return !bWeaponFired;
}

void UMissileGuidanceComponent::JettisonInert(const FVector& EjectionVelocity)
{
	if (!CanDetachWeapon()) return;
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && GetOwner() && !GetOwner()->HasAuthority()) return;
	ActivateWeapon(false);
	if (!UpdatedComponent && GetOwner()) SetUpdatedComponent(GetOwner()->GetRootComponent());
	DetachWeapon();
	AppliedEjectionImpulse = EjectionVelocity;
	Velocity = (IsValid(PlayerAircraft) ? PlayerAircraft->GetVelocity() : FVector::ZeroVector) + EjectionVelocity;
	ProjectileGravityScale = 1.0f;
	MaxSpeed = 0.0f;
	SetActive(true);
	SetComponentTickEnabled(true);
	UpdateComponentVelocity();
}

bool UMissileGuidanceComponent::FireWeapon()
{
	if (!IsDirectFirePermitted()) return false;
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && GetOwner() && !GetOwner()->HasAuthority()) return false;
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
	FRotator FuzeRotation;
	GetSeekerTransform(PreviousFuzeLocation, FuzeRotation);
	PreviousFuzeFlightTime = 0.0f;
	bHasPreviousFuzeSample = true;
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
		// Check root component first
		if (const USceneComponent* RootComp = InTarget->GetRootComponent())
		{
			if (RootComp->DoesSocketExist(TargetTrackingSocket))
			{
				return RootComp->GetSocketLocation(TargetTrackingSocket);
			}
		}

		// Check remaining scene components
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
	return EvaluateProximityFuze(TimeSinceFired);
}

bool UMissileGuidanceComponent::EvaluateProximityFuze(float SampleFlightTime)
{
	if (!bWeaponFired || bFuzeTriggered) return false;
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && GetOwner() && !GetOwner()->HasAuthority()) return false;

	UWorld* World = GetWorld();
	if (!World) return false;

	FVector SeekerLocation = FVector::ZeroVector;
	FRotator SeekerRotation = FRotator::ZeroRotator;
	GetSeekerTransform(SeekerLocation, SeekerRotation);
	FVector SweepStart = bHasPreviousFuzeSample && SampleFlightTime >= PreviousFuzeFlightTime
		? PreviousFuzeLocation : SeekerLocation;
	const float StartFlightTime = bHasPreviousFuzeSample ? PreviousFuzeFlightTime : SampleFlightTime;
	PreviousFuzeLocation = SeekerLocation;
	PreviousFuzeFlightTime = SampleFlightTime;
	bHasPreviousFuzeSample = true;

	if (!bEnableProximityFuze || ProximityFuzeRadius <= 0.0f || SampleFlightTime < FuzeArmingDelay ||
		(bFuzeOnlyTriggersOnLockedTarget && !IsValid(LockedTarget))) return false;

	// Do not detonate for a contact passed before the fuze armed during this frame.
	if (StartFlightTime < FuzeArmingDelay && SampleFlightTime > StartFlightTime)
	{
		SweepStart = FMath::Lerp(SweepStart, SeekerLocation,
			FMath::Clamp((FuzeArmingDelay - StartFlightTime) / (SampleFlightTime - StartFlightTime), 0.0f, 1.0f));
	}
	const FVector Travel = SeekerLocation - SweepStart;
	const bool bHasTravel = !Travel.IsNearlyZero();
	const FVector QueryLocation = (SweepStart + SeekerLocation) * 0.5f;
	const FQuat QueryRotation = bHasTravel
		? FQuat::FindBetweenNormals(FVector::UpVector, Travel.GetSafeNormal()) : FQuat::Identity;
	// A capsule is the complete volume swept by the sphere. An overlap collects every
	// candidate, including those beyond an excluded actor that blocks a channel sweep.
	const FCollisionShape QueryShape = bHasTravel
		? FCollisionShape::MakeCapsule(ProximityFuzeRadius, Travel.Size() * 0.5f + ProximityFuzeRadius)
		: FCollisionShape::MakeSphere(ProximityFuzeRadius);

	if (bEnableDebugTraces)
	{
		if (bHasTravel)
		{
			DrawDebugCapsule(World, QueryLocation,
				QueryShape.GetCapsuleHalfHeight(), ProximityFuzeRadius, QueryRotation,
				FColor::Orange, false, -1.0f, 0, 1.0f);
		}
		else DrawDebugSphere(World, SeekerLocation, ProximityFuzeRadius, 16, FColor::Orange, false, -1.0f, 0, 1.0f);
	}

	FCollisionQueryParams FuzeQueryParams(SCENE_QUERY_STAT(MissileProximityFuze), false, GetOwner());
	FuzeQueryParams.bFindInitialOverlaps = true;
	PopulateSeekerIgnoredActors(FuzeQueryParams);

	TArray<FOverlapResult> OverlapResults;
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectParams.AddObjectTypesToQuery(ECC_Vehicle);

	if (bQueryAllDynamicObjects)
		World->OverlapMultiByObjectType(OverlapResults, QueryLocation,
			QueryRotation, ObjectParams, QueryShape, FuzeQueryParams);
	if (!bQueryAllDynamicObjects || OverlapResults.IsEmpty())
		World->OverlapMultiByChannel(OverlapResults, QueryLocation,
			QueryRotation, DetectionChannel, QueryShape, FuzeQueryParams);

	TArray<AActor*> Candidates;
	for (const FOverlapResult& Overlap : OverlapResults) Candidates.AddUnique(Overlap.GetActor());
	for (AActor* Candidate : Candidates)
	{
		if (!IsFuzeTargetEligible(Candidate))
		{
			continue;
		}

		if (bFuzeOnlyTriggersOnLockedTarget && Candidate != LockedTarget)
		{
			continue;
		}

		// Line-of-sight check against terrain and static obstacles
		const FVector TargetLocation = GetTargetTrackingLocation(Candidate);
		const FVector FuzeLocation = FMath::ClosestPointOnSegment(TargetLocation, SweepStart, SeekerLocation);
		FHitResult HitResult;
		const bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			FuzeLocation,
			TargetLocation,
			ECC_Visibility,
			FuzeQueryParams
		);
		if (bEnableDebugTraces)
			DrawDebugLine(World, FuzeLocation, bHit ? HitResult.ImpactPoint : TargetLocation,
				bHit ? FColor::Red : FColor::Green, false, -1.0f, 0, 1.0f);

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

	bool bHasAuthority = true;
	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
		bHasAuthority = !GetOwner() || GetOwner()->HasAuthority();

	// 1. Proximity fuze detection (Server Authoritative)
	if (bHasAuthority && bWeaponFired && !bFuzeTriggered)
	{
		// Projectile movement has just advanced; guidance updates the flight clock later below.
		const bool bFuzeFired = EvaluateProximityFuze(TimeSinceFired + DeltaTime);
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
