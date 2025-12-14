// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#include "Weapon.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AWeapon::AWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	SetReplicateMovement(true);

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);

	// Default to NoCollision to avoid physics depenetration impulses while mounted
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetSimulatePhysics(false);
	WeaponMesh->SetGenerateOverlapEvents(true);
}

void AWeapon::BeginPlay()
{
	Super::BeginPlay();

	// Check if attached or owned by carrier
	AActor* ParentOrCarrier = GetAttachParentActor();
	if (!ParentOrCarrier && CarrierAircraft.IsValid())
	{
		ParentOrCarrier = CarrierAircraft.Get();
	}
	else if (!ParentOrCarrier && GetOwner() && GetOwner() != this)
	{
		ParentOrCarrier = GetOwner();
	}

	if (ParentOrCarrier)
	{
		SetMounted(true, ParentOrCarrier);
	}
	else if (!bIsMounted)
	{
		// Standalone weapon in world: enable released collision
		ApplyReleasedCollisionState();
	}
}

void AWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SeparationSafetyTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AWeapon::SetCarrierAircraft(AActor* InCarrier)
{
	CarrierAircraft = InCarrier;
	if (bPermanentlyIgnoreCarrier && InCarrier)
	{
		SetupMutualCollisionIgnore(InCarrier, true);
	}
}

void AWeapon::SetMounted(bool bInMounted, AActor* InCarrier)
{
	if (InCarrier)
	{
		CarrierAircraft = InCarrier;
	}

	bIsMounted = bInMounted;

	if (bIsMounted)
	{
		ApplyMountedCollisionState();
	}
	else
	{
		ApplyReleasedCollisionState();
	}

	OnWeaponMountedStateChanged.Broadcast(bIsMounted, CarrierAircraft.Get());
	OnMountedStateChanged(bIsMounted, CarrierAircraft.Get());
}

void AWeapon::ApplyMountedCollisionState()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SeparationSafetyTimerHandle);
	}

	if (WeaponMesh)
	{
		WeaponMesh->SetSimulatePhysics(false);
		if (bDisableCollisionWhileMounted)
		{
			WeaponMesh->SetCollisionEnabled(MountedCollision);
		}
	}

	if (CarrierAircraft.IsValid())
	{
		SetupMutualCollisionIgnore(CarrierAircraft.Get(), true);
	}
}

void AWeapon::ApplyReleasedCollisionState()
{
	// Ignore carrier collision before altering profile
	if (CarrierAircraft.IsValid())
	{
		SetupMutualCollisionIgnore(CarrierAircraft.Get(), true);
	}

	UWorld* World = GetWorld();
	if (World) World->GetTimerManager().ClearTimer(SeparationSafetyTimerHandle);
	if (SeparationSafetyDelay > 0.0f && World)
	{
		World->GetTimerManager().SetTimer(
			SeparationSafetyTimerHandle,
			this, &AWeapon::EnableReleasedCollision,
			SeparationSafetyDelay,
			false
		);
	}
	else
	{
		EnableReleasedCollision();
	}
}

void AWeapon::EnableReleasedCollision()
{
	if (!IsValid(this) || bIsMounted || !IsValid(WeaponMesh)) return;
	if (ReleasedCollisionProfile != NAME_None)
	{
		WeaponMesh->SetCollisionProfileName(ReleasedCollisionProfile);
	}
	WeaponMesh->SetCollisionEnabled(ReleasedCollision);
	if (bSimulatePhysicsOnRelease) WeaponMesh->SetSimulatePhysics(true);
}

void AWeapon::SetupMutualCollisionIgnore(AActor* OtherActor, bool bShouldIgnore)
{
	if (!IsValid(OtherActor) || OtherActor == this)
	{
		return;
	}

	if (WeaponMesh)
	{
		WeaponMesh->IgnoreActorWhenMoving(OtherActor, bShouldIgnore);

		TInlineComponentArray<UPrimitiveComponent*, 16> OtherPrimitives(OtherActor);
		for (UPrimitiveComponent* OtherPrim : OtherPrimitives)
		{
			if (IsValid(OtherPrim))
			{
				WeaponMesh->IgnoreComponentWhenMoving(OtherPrim, bShouldIgnore);
				OtherPrim->IgnoreComponentWhenMoving(WeaponMesh, bShouldIgnore);
				OtherPrim->IgnoreActorWhenMoving(this, bShouldIgnore);
			}
		}
	}
}

void AWeapon::ClearMutualCollisionIgnore(AActor* OtherActor)
{
	SetupMutualCollisionIgnore(OtherActor, false);
}

void AWeapon::OnMountedStateChanged_Implementation(bool bNewMounted, AActor* Carrier)
{
}

void AWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWeapon, bIsMounted);
	DOREPLIFETIME(AWeapon, CarrierAircraft);
}

void AWeapon::OnRep_IsMounted()
{
	if (bIsMounted)
	{
		ApplyMountedCollisionState();
	}
	else
	{
		ApplyReleasedCollisionState();
	}

	OnWeaponMountedStateChanged.Broadcast(bIsMounted, CarrierAircraft.Get());
	OnMountedStateChanged(bIsMounted, CarrierAircraft.Get());
}

void AWeapon::OnRep_CarrierAircraft()
{
	if (bPermanentlyIgnoreCarrier && CarrierAircraft.IsValid())
	{
		SetupMutualCollisionIgnore(CarrierAircraft.Get(), true);
	}
}
