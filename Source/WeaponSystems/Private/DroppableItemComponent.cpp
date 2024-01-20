// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "DroppableItemComponent.h"
#include "Weapon.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Net/UnrealNetwork.h"

UDroppableItemComponent::UDroppableItemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = false;

	// Ballistic projectile movement configuration
	ProjectileGravityScale = 1.0f; // Full 100% gravity
	bRotationFollowsVelocity = true; // Align rotation with trajectory arc
	bShouldBounce = true; // Bouncing enabled by default for realistic drop physics
	Bounciness = 0.35f;
	Friction = 0.45f;
	BounceVelocityStopSimulatingThreshold = 15.0f;
	bBounceAngleAffectsFriction = true;
	InitialSpeed = 0.0f;
	Velocity = FVector::ZeroVector;

	// Droppable stores (tanks, unguided bombs) do not require target locks
	bRequireLockToFire = false;

	SetIsReplicatedByDefault(true);
}

void UDroppableItemComponent::BeginPlay()
{
	Super::BeginPlay();

	OnProjectileBounce.AddDynamic(this, &UDroppableItemComponent::HandleProjectileBounce);
}

void UDroppableItemComponent::HandleProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	OnStoreBounced.Broadcast(ImpactResult, ImpactVelocity);
}

void UDroppableItemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsDetached && bSmoothSeparationAttitude && UpdatedComponent && !Velocity.IsNearlyZero())
	{
		// Smooth initial transition into trajectory arc rather than an instant frame-0 snap
		if (TimeSinceFired < 0.6f)
		{
			const FRotator CurrentRot = UpdatedComponent->GetComponentRotation();
			const FRotator TargetRot = Velocity.Rotation();
			const FRotator SmoothRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, SeparationAlignmentRate);
			UpdatedComponent->SetWorldRotation(SmoothRot);
		}
	}
}

void UDroppableItemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UDroppableItemComponent, bIsDetached);
}

bool UDroppableItemComponent::CanFireWeapon() const
{
	if (bIsDetached || bWeaponFired)
	{
		return false;
	}

	return Super::CanFireWeapon();
}

bool UDroppableItemComponent::FireWeapon()
{
	if (!CanFireWeapon())
	{
		return false;
	}

	DetachWeapon();

	OnWeaponFired.Broadcast(nullptr);
	return true;
}

bool UDroppableItemComponent::CanDetachWeapon() const
{
	if (bIsDetached || bWeaponFired)
	{
		return false;
	}

	return Super::CanDetachWeapon();
}

void UDroppableItemComponent::DetachWeapon()
{
	if (!CanDetachWeapon())
	{
		return;
	}

	APawn* Aircraft = ResolveAircraft();
	if (!IsValid(PlayerAircraft) && Aircraft)
	{
		PlayerAircraft = Aircraft;
	}

	// Detach physically from parent actor if still mounted/attached
	if (AActor* OwnerActor = GetOwner())
	{
		LaunchRotation = OwnerActor->GetActorRotation();

		if (OwnerActor->GetAttachParentActor())
		{
			OwnerActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}

		if (Aircraft)
		{
			if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()))
			{
				RootPrim->IgnoreActorWhenMoving(Aircraft, true);
				TInlineComponentArray<UPrimitiveComponent*, 16> AircraftPrimitives(Aircraft);
				for (UPrimitiveComponent* AirPrim : AircraftPrimitives)
				{
					if (IsValid(AirPrim))
					{
						RootPrim->IgnoreComponentWhenMoving(AirPrim, true);
						AirPrim->IgnoreComponentWhenMoving(RootPrim, true);
						AirPrim->IgnoreActorWhenMoving(OwnerActor, true);
					}
				}
			}

			if (AWeapon* Weapon = Cast<AWeapon>(OwnerActor))
			{
				Weapon->SetupMutualCollisionIgnore(Aircraft, true);
				Weapon->SetMounted(false, Aircraft);
			}
		}
	}

	// Ensure UpdatedComponent is assigned so the root component moves with projectile simulation
	if (!UpdatedComponent && GetOwner())
	{
		SetUpdatedComponent(GetOwner()->GetRootComponent());
	}

	// Inherit carrier aircraft velocity at hardpoint location (including aircraft angular rates)
	if (bInheritAircraftVelocity && Aircraft)
	{
		FVector AircraftVel = FVector::ZeroVector;
		const FVector StoreLocation = GetOwner() ? GetOwner()->GetActorLocation() : Aircraft->GetActorLocation();

		if (const UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Aircraft->GetRootComponent()))
		{
			AircraftVel = RootPrim->IsSimulatingPhysics() ? RootPrim->GetPhysicsLinearVelocityAtPoint(StoreLocation) : RootPrim->GetPhysicsLinearVelocity();
			if (AircraftVel.IsNearlyZero())
			{
				AircraftVel = Aircraft->GetVelocity();
			}
		}
		else
		{
			AircraftVel = Aircraft->GetVelocity();
		}

		Velocity += AircraftVel * InheritedSpeedMultiplier;
	}

	// Configure bounce parameters
	bShouldBounce = bEnableBounce;
	Bounciness = StoreBounciness;
	Friction = StoreFriction;
	BounceVelocityStopSimulatingThreshold = BounceStopVelocityThreshold;
	bBounceAngleAffectsFriction = bStoreBounceAngleAffectsFriction;

	// Ensure gravity is at 100% (1.0)
	ProjectileGravityScale = 1.0f;

	// Activate projectile movement component to begin simulating ballistic flight
	ActivateWeapon(true);
	UpdateComponentVelocity();

	bWeaponFired = true;
	bIsDetached = true;

	Super::DetachWeapon();
}

APawn* UDroppableItemComponent::ResolveAircraft() const
{
	if (IsValid(PlayerAircraft))
	{
		return PlayerAircraft;
	}

	if (const AActor* OwnerActor = GetOwner())
	{
		if (APawn* ParentPawn = Cast<APawn>(OwnerActor->GetAttachParentActor()))
		{
			return ParentPawn;
		}

		if (APawn* InstigatorPawn = OwnerActor->GetInstigator())
		{
			return InstigatorPawn;
		}

		if (APawn* OwnerPawn = Cast<APawn>(OwnerActor->GetOwner()))
		{
			return OwnerPawn;
		}
	}

	return nullptr;
}
