// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "MasterWeaponComponent.h"
#include "Weapon.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Net/UnrealNetwork.h"

UMasterWeaponComponent::UMasterWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = false;
	SetIsReplicatedByDefault(true);
}

void UMasterWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMasterWeaponComponent, bIsWeaponActivated);
	DOREPLIFETIME(UMasterWeaponComponent, bWeaponFired);
}

void UMasterWeaponComponent::ServerFireWeapon_Implementation()
{
	FireWeapon();
}

void UMasterWeaponComponent::InitializeWeapon(APawn* InPlayerAircraft)
{
	PlayerAircraft = InPlayerAircraft;

	if (AWeapon* Weapon = Cast<AWeapon>(GetOwner()))
	{
		Weapon->SetCarrierAircraft(InPlayerAircraft);
	}
}

void UMasterWeaponComponent::ActivateWeapon(bool bActivate)
{
	bIsWeaponActivated = bActivate;

	if (bIsWeaponActivated)
	{
		SetActive(true);
		SetComponentTickEnabled(true);
	}
	else
	{
		SetActive(false);
		SetComponentTickEnabled(false);
		bWeaponFired = false;
		TimeSinceFired = 0.0f;
	}
}

bool UMasterWeaponComponent::CanFireWeapon() const
{
	return true;
}

bool UMasterWeaponComponent::FireWeapon()
{
	return false;
}

bool UMasterWeaponComponent::CanDetachWeapon() const
{
	return true;
}

void UMasterWeaponComponent::DetachWeapon()
{
	if (!CanDetachWeapon())
	{
		return;
	}

	if (AActor* OwnerActor = GetOwner())
	{
		LaunchRotation = OwnerActor->GetActorRotation();

		if (OwnerActor->GetAttachParentActor())
		{
			OwnerActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}

		if (IsValid(PlayerAircraft))
		{
			if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()))
			{
				RootPrim->IgnoreActorWhenMoving(PlayerAircraft, true);
				TInlineComponentArray<UPrimitiveComponent*, 16> AircraftPrimitives(PlayerAircraft);
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
		}

		if (AWeapon* Weapon = Cast<AWeapon>(OwnerActor))
		{
			Weapon->SetupMutualCollisionIgnore(PlayerAircraft, true);
			Weapon->SetMounted(false, PlayerAircraft);
		}
	}

	OnWeaponDetached.Broadcast(GetOwner());
}

void UMasterWeaponComponent::ApplyEjectionImpulse(const FVector& Impulse)
{
	AppliedEjectionImpulse = Impulse;

	if (!Impulse.IsNearlyZero())
	{
		Velocity += Impulse;
		UpdateComponentVelocity();
	}
}
