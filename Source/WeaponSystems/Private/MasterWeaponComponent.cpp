// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#include "MasterWeaponComponent.h"
#include "ModularMissionManagement.h"
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

bool UMasterWeaponComponent::SlaveToDirection(const FVector&) { return false; }
bool UMasterWeaponComponent::SlaveToLocation(const FVector&) { return false; }
bool UMasterWeaponComponent::SlaveToTarget(AActor*) { return false; }
void UMasterWeaponComponent::SlaveToBoresight() {}
void UMasterWeaponComponent::SetSeekerCaged(bool) {}
bool UMasterWeaponComponent::IsSeekerCaged() const
{
	return SeekerState == EWeaponSeekerState::Caged || SeekerState == EWeaponSeekerState::Standby;
}
FVector UMasterWeaponComponent::GetSeekerLookDirection() const
{
	return UpdatedComponent ? UpdatedComponent->GetForwardVector() :
		(GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector);
}
FVector2D UMasterWeaponComponent::GetSeekerGimbalAngles() const { return FVector2D::ZeroVector; }
float UMasterWeaponComponent::GetSeekerGimbalLimitAngle() const { return 0.0f; }
EWeaponAudioTone UMasterWeaponComponent::GetSeekerAudioTone() const { return EWeaponAudioTone::Silent; }
float UMasterWeaponComponent::GetSeekerSignalStrength() const { return 0.0f; }
bool UMasterWeaponComponent::GetDynamicLaunchZone(const AActor*, float& OutRmin, float& OutRne, float& OutRmax) const
{
	OutRmin = OutRne = OutRmax = 0.0f;
	return false;
}
bool UMasterWeaponComponent::IsTargetInLaunchEnvelope(const AActor* Target) const
{
	float Rmin, Rne, Rmax;
	return IsValid(Target) && GetDynamicLaunchZone(Target, Rmin, Rne, Rmax) &&
		FVector::Dist(GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector,
			Target->GetActorLocation()) >= Rmin &&
		FVector::Dist(GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector,
			Target->GetActorLocation()) <= Rmax;
}
float UMasterWeaponComponent::GetEstimatedTimeToImpact(const AActor*) const { return 0.0f; }
float UMasterWeaponComponent::GetEstimatedTimeToActive(const AActor*) const { return 0.0f; }

void UMasterWeaponComponent::TransitionSeekerState(EWeaponSeekerState NewState, AActor* TrackedTarget)
{
	if (NewState != EWeaponSeekerState::Tracking) TrackedTarget = nullptr;
	if (ReportedLockTarget.Get() != TrackedTarget)
	{
		if (AActor* OldTarget = ReportedLockTarget.Get())
			OnLockLost.Broadcast(this, IsValid(OldTarget) ? OldTarget : nullptr);
		ReportedLockTarget = TrackedTarget;
		LastObservedLockTarget = TrackedTarget;
		if (IsValid(TrackedTarget)) OnLockAcquired.Broadcast(this, TrackedTarget);
	}
	if (SeekerState == NewState) return;
	const EWeaponSeekerState OldState = SeekerState;
	SeekerState = NewState;
	OnSeekerStateChanged.Broadcast(this, OldState, NewState);
	if (OldState != EWeaponSeekerState::Standby && NewState != EWeaponSeekerState::Standby &&
		(OldState == EWeaponSeekerState::Caged) != (NewState == EWeaponSeekerState::Caged))
		OnCageStateChanged.Broadcast(this, NewState == EWeaponSeekerState::Caged);
}

void UMasterWeaponComponent::OnRep_SeekerState(EWeaponSeekerState OldState)
{
	if (OldState == SeekerState) return;
	OnSeekerStateChanged.Broadcast(this, OldState, SeekerState);
	if (OldState != EWeaponSeekerState::Standby && SeekerState != EWeaponSeekerState::Standby &&
		(OldState == EWeaponSeekerState::Caged) != (SeekerState == EWeaponSeekerState::Caged))
		OnCageStateChanged.Broadcast(this, SeekerState == EWeaponSeekerState::Caged);
}

void UMasterWeaponComponent::OnRep_ReportedLockTarget()
{
	if (AActor* OldTarget = LastObservedLockTarget.Get(); OldTarget != ReportedLockTarget.Get())
	{
		if (IsValid(OldTarget)) OnLockLost.Broadcast(this, OldTarget);
		if (IsValid(ReportedLockTarget)) OnLockAcquired.Broadcast(this, ReportedLockTarget);
	}
	LastObservedLockTarget = ReportedLockTarget;
}

void UMasterWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMasterWeaponComponent, bIsWeaponActivated);
	DOREPLIFETIME(UMasterWeaponComponent, bWeaponFired);
	DOREPLIFETIME_CONDITION(UMasterWeaponComponent, SeekerState, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMasterWeaponComponent, ReportedLockTarget, COND_OwnerOnly);
}

void UMasterWeaponComponent::ServerFireWeapon_Implementation()
{
	ClientFireWeaponResult(RequestFireWeapon());
}

void UMasterWeaponComponent::ClientFireWeaponResult_Implementation(bool bSucceeded)
{
	OnWeaponFireResult.Broadcast(bSucceeded);
}

bool UMasterWeaponComponent::RequestFireWeapon()
{
	AActor* WeaponActor = GetOwner();
	if (!IsValid(WeaponActor)) return false;
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && !WeaponActor->HasAuthority())
	{
		ServerFireWeapon();
		return true;
	}
	if (UModularMissionManagement* Mission = GetMountedMission())
	{
		return Mission->FireMountedWeapon(this);
	}
	if (const AWeapon* MountedWeapon = Cast<AWeapon>(WeaponActor); MountedWeapon && MountedWeapon->IsMounted()) return false;
	return FireWeapon();
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

bool UMasterWeaponComponent::CanFireWeapon(EWeaponLaunchFailureReason& OutReason) const
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
	OutReason = EWeaponLaunchFailureReason::None;
	return true;
}

bool UMasterWeaponComponent::FireWeapon()
{
	return false;
}

bool UMasterWeaponComponent::IsDirectFirePermitted() const
{
	const UWorld* World = GetWorld();
	const AWeapon* WeaponActor = Cast<AWeapon>(GetOwner());
	return !World || !World->IsGameWorld() || bMissionReleaseAuthorized ||
		(!GetMountedMission() && (!WeaponActor || !WeaponActor->IsMounted()));
}

UModularMissionManagement* UMasterWeaponComponent::GetMountedMission() const
{
	AActor* WeaponActor = GetOwner();
	if (!IsValid(WeaponActor)) return nullptr;
	AActor* Carrier = nullptr;
	if (const AWeapon* Weapon = Cast<AWeapon>(WeaponActor))
	{
		if (!Weapon->IsMounted()) return nullptr;
		Carrier = Weapon->GetCarrierAircraft();
	}
	else
	{
		Carrier = WeaponActor->GetAttachParentActor();
	}
	return IsValid(Carrier) ? Carrier->FindComponentByClass<UModularMissionManagement>() : nullptr;
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

void UMasterWeaponComponent::OnRep_WeaponFired()
{
	if (bWeaponFired)
	{
		OnWeaponFired.Broadcast(nullptr);
	}
}

void UMasterWeaponComponent::OnRep_IsWeaponActivated()
{
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
