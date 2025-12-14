// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#include "ModularMissionManagement.h"
#include "Weapon.h"
#include "MissileGuidanceComponent.h"
#include "RadarMissileGuidanceComponent.h"
#include "IRMissileGuidanceComponent.h"
#include "ARMMissileGuidanceComponent.h"
#include "RadarWarningReceiverComponent.h"
#include "AircraftRadarComponent.h"
#include "MasterWeaponComponent.h"
#include "DroppableItemComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "AircraftCombatDebug.h"

namespace
{
	bool HasInventoryAuthority(const UModularMissionManagement* Mission)
	{
		if (UWorld* World = Mission->GetWorld(); World && World->IsGameWorld())
			return IsValid(Mission->GetOwner()) && Mission->GetOwner()->HasAuthority();
		return true;
	}
	bool IsAvailableStation(const FWeaponStation& Station)
	{
		return Station.StoreType != EStoreType::None && Station.GetCurrentAmmo() > 0 &&
			Station.Status != EStationStatus::Fault && Station.Status != EStationStatus::Jettisoned;
	}
	UMasterWeaponComponent* ResolveStoreWeapon(AActor* Actor)
	{
		if (!IsValid(Actor)) return nullptr;
		// Multiple movement/seeker components are ambiguous; reject rather than
		// picking a component based on actor component order.
		TInlineComponentArray<UMasterWeaponComponent*> Weapons(Actor);
		return Weapons.Num() == 1 && IsValid(Weapons[0]) ? Weapons[0] : nullptr;
	}
	bool HasRequiredWeaponComponent(EStoreType Type, const UMasterWeaponComponent* Weapon)
	{
		switch (Type)
		{
		case EStoreType::AirToAirMissile_IR: return Cast<UIRMissileGuidanceComponent>(Weapon) != nullptr;
		case EStoreType::AirToAirMissile_Radar: return Cast<URadarMissileGuidanceComponent>(Weapon) != nullptr;
		case EStoreType::AntiRadiationMissile: return Cast<UARMMissileGuidanceComponent>(Weapon) != nullptr;
		case EStoreType::AirToGroundMissile: return Cast<UMissileGuidanceComponent>(Weapon) != nullptr;
		case EStoreType::Bomb_Guided: return IsValid(Weapon);
		default: return true; // Plain physics actors are supported for unguided stores.
		}
	}
}

UModularMissionManagement::UModularMissionManagement()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetIsReplicatedByDefault(true);

	MasterArmMode = EMasterArmMode::Safe;
	MasterMode = EAircraftMasterMode::AirToAir;
	bAutoSpawnStoresOnBeginPlay = true;
	bAutoStepStationOnFire = true;
	SelectedStationIndex = 1;
}

void UModularMissionManagement::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UModularMissionManagement, MasterArmMode);
	DOREPLIFETIME(UModularMissionManagement, MasterMode);
	DOREPLIFETIME(UModularMissionManagement, GunFiringState);

	DOREPLIFETIME_CONDITION(UModularMissionManagement, Stations, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UModularMissionManagement, SelectedStationIndex, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UModularMissionManagement, LockedTarget, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UModularMissionManagement, BuggedTarget, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UModularMissionManagement, bInhibitFriendlyFire, COND_OwnerOnly);
}

void UModularMissionManagement::OnRep_MasterArmMode()
{
	OnMasterArmChanged.Broadcast(MasterArmMode);
}

void UModularMissionManagement::OnRep_MasterMode()
{
	OnMasterModeChanged.Broadcast(MasterMode);
}

void UModularMissionManagement::OnRep_Stations()
{
	// Ammo/status replication is frequent. Reuse presentation components unless
	// their mesh, socket, or parent configuration actually changed.
	USceneComponent* Mesh = ResolveAircraftMesh();
	int32 PylonIndex = 0;
	bool bPylonsMatch = true;
	for (const FWeaponStation& Station : Stations)
	{
		for (const FStationPylon& Config : Station.Pylons)
		{
			if (!Config.PylonMesh) continue;
			UStaticMeshComponent* Pylon = LocalPylons.IsValidIndex(PylonIndex) ? LocalPylons[PylonIndex].Get() : nullptr;
			bPylonsMatch &= IsValid(Pylon) && Pylon->GetStaticMesh() == Config.PylonMesh &&
				Pylon->GetAttachSocketName() == Config.SocketName && Pylon->GetAttachParent() == Mesh;
			++PylonIndex;
		}
	}
	bPylonsMatch &= PylonIndex == LocalPylons.Num();
	if (bPylonsMatch)
	{
		PylonIndex = 0;
		for (FWeaponStation& Station : Stations)
		{
			Station.SpawnedPylons.Reset();
			for (const FStationPylon& Config : Station.Pylons)
				if (Config.PylonMesh) Station.SpawnedPylons.Add(LocalPylons[PylonIndex++]);
		}
	}
	else SpawnAllPylons();
	ProgramAllWeaponsIgnoreLists();
	BindActiveWeaponDelegates();
	OnStoresInventoryChanged.Broadcast();
}

void UModularMissionManagement::OnRep_SelectedStationIndex()
{
	PilotCuedWeapon.Reset();
	AutomaticallyCuedWeapon.Reset();
	BindActiveWeaponDelegates();
	FWeaponStation StationData;
	if (GetSelectedStation(StationData))
	{
		OnStationSelected.Broadcast(SelectedStationIndex, StationData);
	}
}

void UModularMissionManagement::OnRep_LockedTarget()
{
	PilotCuedWeapon.Reset();
	if (IsValid(LockedTarget))
	{
		OnTargetLocked.Broadcast(LockedTarget);
	}
	else
	{
		OnLockCleared.Broadcast();
	}
	RefreshSelectedWeaponCue();
}

void UModularMissionManagement::OnRep_BuggedTarget()
{
	PilotCuedWeapon.Reset();
	if (IsValid(BuggedTarget)) OnTargetBugged.Broadcast(BuggedTarget);
	else OnBugCleared.Broadcast();
	RefreshSelectedWeaponCue();
}

void UModularMissionManagement::OnRep_GunFiringState()
{
	// Clients play presentation events only; the server owns every gun timer and round.
	if (GunFiringState.bIsFiring) OnGunFiringStarted.Broadcast(GunFiringState.StationIndex);
	else OnGunFiringStopped.Broadcast(GunFiringState.StationIndex);
}

void UModularMissionManagement::BeginPlay()
{
	Super::BeginPlay();

	// Auto-discover aircraft radar and RWR if present
	if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
	{
		if (GetOwner() && (!GetWorld() || !GetWorld()->IsGameWorld() || GetOwner()->HasAuthority()))
		{
			Radar->OnRadarLockAcquired.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarLockAcquired);
			Radar->OnRadarLockLost.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarLockLost);
			Radar->OnRadarTrackSelected.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarTrackSelected);
			Radar->OnRadarTrackDeselected.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarTrackDeselected);
			if (AActor* Target = Radar->GetSTTLockedActor()) LockActor(Target);
			FRadarTrack Bug;
			if (Radar->GetBuggedTrackID() >= 0 && Radar->GetTrackByID(Radar->GetBuggedTrackID(), Bug))
				BugActor(Bug.TrackedActor.Get());
		}
	}
	ResolveRWRComponent();

	if (bAutoSpawnStoresOnBeginPlay)
	{
		InitializeStores();
	}
	for (FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::InternalCannon) Station.SimulatedAmmo = Station.CurrentAmmo;
	}

	// Select first available station with ammunition if available
	if (Stations.Num() > 0 && HasInventoryAuthority(this))
	{
		bool bFoundSelected = false;
		for (const FWeaponStation& Station : Stations)
		{
			if (IsAvailableStation(Station))
			{
				SelectStation(Station.StationIndex);
				bFoundSelected = true;
				break;
			}
		}

		if (!bFoundSelected && Stations.IsValidIndex(0))
		{
			SelectStation(Stations[0].StationIndex);
		}
	}
}

void UModularMissionManagement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ActiveTracedBullets.IsEmpty())
	{
		SetComponentTickEnabled(false);
		return;
	}

	SimulateTracedBullets(DeltaTime);

	if (ActiveTracedBullets.IsEmpty())
	{
		SetComponentTickEnabled(false);
	}
}

void UModularMissionManagement::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bStoreReleaseInProgress = false;
	UnbindActiveWeaponDelegates();
	ActiveTracedBullets.Empty();
	if (UWorld* World = GetWorld())
	{
		for (auto& Pair : ActiveGunTimers)
		{
			World->GetTimerManager().ClearTimer(Pair.Value);
		}
	}
	ActiveGunTimers.Empty();
	GunMuzzleIndices.Empty();

	if (IsValid(LockedTarget))
	{
		LockedTarget->OnDestroyed.RemoveDynamic(this, &UModularMissionManagement::HandleCueTargetDestroyed);
	}
	LockedTarget = nullptr;
	if (IsValid(BuggedTarget))
		BuggedTarget->OnDestroyed.RemoveDynamic(this, &UModularMissionManagement::HandleCueTargetDestroyed);
	BuggedTarget = nullptr;
	if (UAircraftRadarComponent* Radar = CachedRadarComponent; IsValid(Radar))
	{
		Radar->OnRadarLockAcquired.RemoveDynamic(this, &UModularMissionManagement::HandleRadarLockAcquired);
		Radar->OnRadarLockLost.RemoveDynamic(this, &UModularMissionManagement::HandleRadarLockLost);
		Radar->OnRadarTrackSelected.RemoveDynamic(this, &UModularMissionManagement::HandleRadarTrackSelected);
		Radar->OnRadarTrackDeselected.RemoveDynamic(this, &UModularMissionManagement::HandleRadarTrackDeselected);
	}

	CachedRadarComponent = nullptr;
	CachedRWRComponent = nullptr;
	if (HasInventoryAuthority(this)) DestroyMountedStores();

	DestroyAllPylons();
	Super::EndPlay(EndPlayReason);
}

USceneComponent* UModularMissionManagement::ResolveAircraftMesh() const
{
	if (IsValid(CachedAircraftMesh)) return CachedAircraftMesh;
	const AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor)) return nullptr;
	TInlineComponentArray<USceneComponent*> Components(OwnerActor);
	// Explicit selection wins over cosmetic meshes and generated pylons.
	for (USceneComponent* Component : Components)
	{
		if (IsValid(Component) && Component->ComponentHasTag(TEXT("AircraftMesh")))
		{
			CachedAircraftMesh = Component;
			return Component;
		}
	}
	for (USceneComponent* Component : Components)
	{
		if (IsValid(Component) && Component->IsA<UMeshComponent>() &&
			!Component->GetName().StartsWith(TEXT("Pylon_Sta")))
		{
			CachedAircraftMesh = Component;
			return Component;
		}
	}
	// Do not pin the fallback: a mesh may be added after this first query.
	return OwnerActor->GetRootComponent();
}

void UModularMissionManagement::InitializeStores(USceneComponent* InAircraftMesh)
{
	if (bStoreReleaseInProgress) return;
	if (IsValid(InAircraftMesh) && InAircraftMesh->GetOwner() == GetOwner())
	{
		CachedAircraftMesh = InAircraftMesh;
	}
	else
	{
		ResolveAircraftMesh();
	}

	SpawnAllPylons();
	SpawnAllStores();
	OnStoresInventoryChanged.Broadcast();
}

void UModularMissionManagement::SpawnStoreForStation(int32 StationIndex)
{
	if (bStoreReleaseInProgress || !HasInventoryAuthority(this)) return;
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		return;
	}

	FWeaponStation& Station = Stations[ArrayIndex];

	if (Station.StoreType == EStoreType::None)
	{
		return;
	}

	// Built-in cannons manage rounds internally and spawn projectiles dynamically on fire
	if (Station.StoreType == EStoreType::InternalCannon)
	{
		Station.bUsesAmmoManagement = true;
		Station.bCanJettison = false;
		return;
	}

	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor || (World->IsGameWorld() && !OwnerActor->HasAuthority()))
	{
		return;
	}

	USceneComponent* MeshComp = ResolveAircraftMesh();

	for (FStationStore& Store : Station.Stores)
	{
		if (Store.Status != EStoreStatus::Ready || !Store.WeaponClass || IsValid(Store.MountedActor))
		{
			continue;
		}

		FTransform SpawnTransform = OwnerActor->GetActorTransform();

		if (MeshComp && Store.SocketName != NAME_None && MeshComp->DoesSocketExist(Store.SocketName))
		{
			SpawnTransform = MeshComp->GetSocketTransform(Store.SocketName);
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = OwnerActor;
		SpawnParams.Instigator = Cast<APawn>(OwnerActor);
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SpawnedActor = World->SpawnActor<AActor>(Store.WeaponClass, SpawnTransform, SpawnParams);
		if (!SpawnedActor)
		{
			continue;
		}

		// Attach to aircraft socket or mesh
		if (MeshComp)
		{
			SpawnedActor->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Store.SocketName);
		}
		else
		{
			SpawnedActor->AttachToActor(OwnerActor, FAttachmentTransformRules::KeepWorldTransform);
		}

		Store.MountedActor = SpawnedActor;

		// Configure safe mounted collision state so stores never induce Chaos physics depenetration impulses
		if (AWeapon* Weapon = Cast<AWeapon>(SpawnedActor))
		{
			Weapon->SetMounted(true, OwnerActor);
		}
		else
		{
			TInlineComponentArray<UPrimitiveComponent*, 8> Prims(SpawnedActor);
			for (UPrimitiveComponent* Prim : Prims)
			{
				if (IsValid(Prim))
				{
					Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Prim->SetSimulatePhysics(false);
				}
			}
		}

		SetupStoreCollisionIgnores(SpawnedActor);

		// Initialize weapon component owner reference
		if (UMasterWeaponComponent* WeaponComp = ResolveStoreWeapon(SpawnedActor))
		{
			WeaponComp->InitializeWeapon(Cast<APawn>(OwnerActor));

			if (URadarMissileGuidanceComponent* RadarGuidance = Cast<URadarMissileGuidanceComponent>(WeaponComp))
			{
				if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
				{
					RadarGuidance->SetParentRadar(Radar);
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("SpawnStoreForStation: Spawned weapon actor %s is missing a UMasterWeaponComponent!"), *SpawnedActor->GetName());
		}
	}

	if (Station.Status != EStationStatus::Fault && Station.Status != EStationStatus::Jettisoned)
		Station.Status = Station.GetCurrentAmmo() > 0 ?
			((Station.StationIndex == SelectedStationIndex) ? EStationStatus::Selected : EStationStatus::Ready) : EStationStatus::Empty;
}

void UModularMissionManagement::SpawnAllPylons()
{
	AActor* OwnerActor = GetOwner();
	USceneComponent* MeshComp = ResolveAircraftMesh();

	if (!OwnerActor || !MeshComp)
	{
		return;
	}
	DestroyAllPylons();

	for (FWeaponStation& Station : Stations)
	{
		for (int32 PylonIdx = 0; PylonIdx < Station.Pylons.Num(); ++PylonIdx)
		{
			const FStationPylon& PylonData = Station.Pylons[PylonIdx];
			if (PylonData.PylonMesh)
			{
				FString CompName = FString::Printf(TEXT("Pylon_Sta%d_%d"), Station.StationIndex, PylonIdx);
				const FName UniqueName = MakeUniqueObjectName(OwnerActor, UStaticMeshComponent::StaticClass(), FName(*CompName));
				UStaticMeshComponent* PylonComp = NewObject<UStaticMeshComponent>(OwnerActor, UniqueName);
				if (PylonComp)
				{
					PylonComp->SetStaticMesh(PylonData.PylonMesh);
					PylonComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					PylonComp->RegisterComponent();
					PylonComp->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, PylonData.SocketName);
					
					Station.SpawnedPylons.Add(PylonComp);
					LocalPylons.Add(PylonComp);
				}
			}
		}
	}
}

void UModularMissionManagement::DestroyAllPylons()
{
	for (UStaticMeshComponent* Pylon : LocalPylons)
		if (IsValid(Pylon)) Pylon->DestroyComponent();
	LocalPylons.Reset();
	for (FWeaponStation& Station : Stations)
	{
		for (UStaticMeshComponent* PylonComp : Station.SpawnedPylons)
		{
			if (IsValid(PylonComp))
			{
				PylonComp->DestroyComponent();
			}
		}
		Station.SpawnedPylons.Empty();
	}
}

void UModularMissionManagement::SpawnAllStores()
{
	if (bStoreReleaseInProgress || !HasInventoryAuthority(this)) return;
	UnbindActiveWeaponDelegates();
	for (FWeaponStation& Station : Stations)
	{
		SpawnStoreForStation(Station.StationIndex);
	}
	ProgramAllWeaponsIgnoreLists();
	PrepareActiveWeaponOnStation(SelectedStationIndex);
	BindActiveWeaponDelegates();
}

void UModularMissionManagement::SetupStoreCollisionIgnores(AActor* StoreActor)
{
	if (!IsValid(StoreActor))
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		// Bidirectional ignore on primitive components
		TInlineComponentArray<UPrimitiveComponent*, 16> StorePrimitives(StoreActor);
		TInlineComponentArray<UPrimitiveComponent*, 16> OwnerPrimitives(OwnerActor);

		for (UPrimitiveComponent* StorePrim : StorePrimitives)
		{
			if (IsValid(StorePrim))
			{
				StorePrim->IgnoreActorWhenMoving(OwnerActor, true);
				for (UPrimitiveComponent* OwnerPrim : OwnerPrimitives)
				{
					if (IsValid(OwnerPrim))
					{
						StorePrim->IgnoreComponentWhenMoving(OwnerPrim, true);
						OwnerPrim->IgnoreComponentWhenMoving(StorePrim, true);
						OwnerPrim->IgnoreActorWhenMoving(StoreActor, true);
					}
				}
			}
		}

		// Also ignore all attached actors to the owner aircraft (pylons, etc.)
		TArray<AActor*> AttachedToOwner;
		OwnerActor->GetAttachedActors(AttachedToOwner, true, true);
		for (AActor* Attached : AttachedToOwner)
		{
			if (IsValid(Attached) && Attached != StoreActor)
			{
				TInlineComponentArray<UPrimitiveComponent*, 8> AttachedPrimitives(Attached);
				for (UPrimitiveComponent* AttPrim : AttachedPrimitives)
				{
					if (IsValid(AttPrim))
					{
						AttPrim->IgnoreActorWhenMoving(StoreActor, true);
						for (UPrimitiveComponent* StorePrim : StorePrimitives)
						{
							if (IsValid(StorePrim))
							{
								StorePrim->IgnoreComponentWhenMoving(AttPrim, true);
								AttPrim->IgnoreComponentWhenMoving(StorePrim, true);
								StorePrim->IgnoreActorWhenMoving(Attached, true);
							}
						}
					}
				}
			}
		}
	}

	// Ignore all spawned pylon components across all stations
	for (const FWeaponStation& Station : Stations)
	{
		for (UStaticMeshComponent* PylonComp : Station.SpawnedPylons)
		{
			if (IsValid(PylonComp))
			{
				TInlineComponentArray<UPrimitiveComponent*, 8> StorePrimitives(StoreActor);
				for (UPrimitiveComponent* StorePrim : StorePrimitives)
				{
					if (IsValid(StorePrim))
					{
						StorePrim->IgnoreComponentWhenMoving(PylonComp, true);
						PylonComp->IgnoreComponentWhenMoving(StorePrim, true);
					}
				}
			}
		}
	}

	// Mutual ignore with all other mounted store actors
	for (const FWeaponStation& Station : Stations)
	{
		for (const FStationStore& Store : Station.Stores)
		{
			if (Store.MountedActor && IsValid(Store.MountedActor) && Store.MountedActor != StoreActor)
			{
				if (AWeapon* WeaponA = Cast<AWeapon>(StoreActor))
				{
					WeaponA->SetupMutualCollisionIgnore(Store.MountedActor, true);
				}
				if (AWeapon* WeaponB = Cast<AWeapon>(Store.MountedActor))
				{
					WeaponB->SetupMutualCollisionIgnore(StoreActor, true);
				}
			}
		}
	}

	if (AWeapon* Weapon = Cast<AWeapon>(StoreActor))
	{
		if (OwnerActor)
		{
			Weapon->SetupMutualCollisionIgnore(OwnerActor, true);
		}
	}
}

void UModularMissionManagement::ProgramWeaponIgnoreList(UMasterWeaponComponent* WeaponComp)
{
	if (!WeaponComp || !bConfigureWeaponsToIgnoreAircraftAndStores)
	{
		return;
	}

	AActor* WeaponActor = WeaponComp->GetOwner();
	if (WeaponActor)
	{
		SetupStoreCollisionIgnores(WeaponActor);
	}

	TArray<AActor*> IgnoredList;
	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		IgnoredList.Add(OwnerActor);

		// Include all attached actors to the owner aircraft
		TArray<AActor*> AttachedToOwner;
		OwnerActor->GetAttachedActors(AttachedToOwner, true, true);
		for (AActor* Attached : AttachedToOwner)
		{
			if (IsValid(Attached))
			{
				IgnoredList.AddUnique(Attached);
			}
		}
	}

	// Include all mounted store actors across all stations
	for (const FWeaponStation& Station : Stations)
	{
		for (const FStationStore& Store : Station.Stores)
		{
			if (Store.MountedActor && IsValid(Store.MountedActor))
			{
				IgnoredList.AddUnique(Store.MountedActor);
			}
		}
	}

	if (UMissileGuidanceComponent* MissileGuidance = Cast<UMissileGuidanceComponent>(WeaponComp))
	{
		MissileGuidance->SetIgnoredActors(IgnoredList);
	}

	if (AWeapon* Weapon = Cast<AWeapon>(WeaponActor))
	{
		for (AActor* Ignored : IgnoredList)
		{
			Weapon->SetupMutualCollisionIgnore(Ignored, true);
		}
	}
}

void UModularMissionManagement::ProgramAllWeaponsIgnoreLists()
{
	if (!bConfigureWeaponsToIgnoreAircraftAndStores)
	{
		return;
	}

	for (const FWeaponStation& Station : Stations)
	{
		for (const FStationStore& Store : Station.Stores)
		{
			if (Store.MountedActor && IsValid(Store.MountedActor))
			{
				if (UMasterWeaponComponent* Comp = ResolveStoreWeapon(Store.MountedActor))
				{
					ProgramWeaponIgnoreList(Comp);
				}
			}
		}
	}
}

void UModularMissionManagement::DestroyMountedStores()
{
	if (bStoreReleaseInProgress || !HasInventoryAuthority(this)) return;
	UnbindActiveWeaponDelegates();
	for (FWeaponStation& Station : Stations)
	{
		for (FStationStore& Store : Station.Stores)
		{
			if (IsValid(Store.MountedActor))
			{
				Store.MountedActor->Destroy();
			}
			Store.MountedActor = nullptr;
		}
	}
}

int32 UModularMissionManagement::FindStationArrayIndex(int32 StationIndex) const
{
	for (int32 Index = 0; Index < Stations.Num(); ++Index)
	{
		if (Stations[Index].StationIndex == StationIndex)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UModularMissionManagement::GetStation(int32 StationIndex, FWeaponStation& OutStation) const
{
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex != INDEX_NONE)
	{
		OutStation = Stations[ArrayIndex];
		return true;
	}
	OutStation = FWeaponStation();
	return false;
}

bool UModularMissionManagement::GetStationBySocket(FName SocketName, FWeaponStation& OutStation) const
{
	OutStation = FWeaponStation();
	if (SocketName == NAME_None) return false;
	for (const FWeaponStation& Station : Stations)
	{
		for (const FStationPylon& Pylon : Station.Pylons)
		{
			if (Pylon.SocketName == SocketName) { OutStation = Station; return true; }
		}
		for (const FGunMuzzle& Muzzle : Station.GunMuzzles)
		{
			if (Muzzle.MuzzleSocketName == SocketName) { OutStation = Station; return true; }
		}
		for (const FStationStore& Store : Station.Stores)
		{
			if (Store.SocketName == SocketName)
			{
				OutStation = Station;
				return true;
			}
		}
	}
	return false;
}

void UModularMissionManagement::GetStationsByStoreType(EStoreType InStoreType, TArray<FWeaponStation>& OutStations) const
{
	OutStations.Reset();
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == InStoreType)
		{
			OutStations.Add(Station);
		}
	}
}

int32 UModularMissionManagement::GetTotalAmmoForStoreType(EStoreType InStoreType) const
{
	int32 Total = 0;
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == InStoreType)
		{
			Total += Station.GetCurrentAmmo();
		}
	}
	return Total;
}

int32 UModularMissionManagement::GetTotalAmmoCount() const
{
	int32 Total = 0;
	for (const FWeaponStation& Station : Stations)
	{
		Total += Station.GetCurrentAmmo();
	}
	return Total;
}

bool UModularMissionManagement::SelectStation(int32 StationIndex)
{
	const int32 TargetIdx = FindStationArrayIndex(StationIndex);
	if (TargetIdx == INDEX_NONE)
	{
		return false;
	}

	// If the previously selected station was actively firing a gun, stop it
	StopFiring(SelectedStationIndex);
	UnbindActiveWeaponDelegates();

	// Deactivate the currently selected weapon before switching
	if (UMasterWeaponComponent* OldWeapon = GetActiveWeaponComponent(SelectedStationIndex))
	{
		OldWeapon->ActivateWeapon(false);
	}

	// Update previous selected station status back to ready or empty
	const int32 PrevIdx = FindStationArrayIndex(SelectedStationIndex);
	if (PrevIdx != INDEX_NONE && PrevIdx != TargetIdx)
	{
		if (Stations[PrevIdx].Status == EStationStatus::Selected)
		{
			Stations[PrevIdx].Status = (Stations[PrevIdx].GetCurrentAmmo() > 0) ? EStationStatus::Ready : EStationStatus::Empty;
			OnStationStatusChanged.Broadcast(Stations[PrevIdx].StationIndex, Stations[PrevIdx].Status);
		}
	}

	SelectedStationIndex = StationIndex;
	if (Stations[TargetIdx].Status != EStationStatus::Fault && Stations[TargetIdx].Status != EStationStatus::Jettisoned)
	{
		Stations[TargetIdx].Status = EStationStatus::Selected;
		OnStationStatusChanged.Broadcast(SelectedStationIndex, Stations[TargetIdx].Status);
	}

	OnStationSelected.Broadcast(SelectedStationIndex, Stations[TargetIdx]);

	// Activate and configure weapon for the newly selected station
	PrepareActiveWeaponOnStation(SelectedStationIndex);
	BindActiveWeaponDelegates();

	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority())
		{
			ServerSelectStation(StationIndex);
		}
	}

	return true;
}

bool UModularMissionManagement::SelectNextStation()
{
	if (Stations.Num() == 0)
	{
		return false;
	}

	const int32 CurrentIdx = FindStationArrayIndex(SelectedStationIndex);
	const int32 StartIdx = (CurrentIdx != INDEX_NONE) ? CurrentIdx : -1;

	for (int32 i = 1; i <= Stations.Num(); ++i)
	{
		const int32 TestIdx = (StartIdx + i) % Stations.Num();
		if (IsAvailableStation(Stations[TestIdx]))
		{
			return SelectStation(Stations[TestIdx].StationIndex);
		}
	}

	return false;
}

bool UModularMissionManagement::SelectPreviousStation()
{
	if (Stations.Num() == 0)
	{
		return false;
	}

	const int32 CurrentIdx = FindStationArrayIndex(SelectedStationIndex);
	const int32 StartIdx = (CurrentIdx != INDEX_NONE) ? CurrentIdx : Stations.Num();

	for (int32 i = 1; i <= Stations.Num(); ++i)
	{
		int32 TestIdx = (StartIdx - i) % Stations.Num();
		if (TestIdx < 0)
		{
			TestIdx += Stations.Num();
		}

		if (IsAvailableStation(Stations[TestIdx]))
		{
			return SelectStation(Stations[TestIdx].StationIndex);
		}
	}

	return false;
}

bool UModularMissionManagement::SelectNextStationOfStoreType(EStoreType InStoreType)
{
	if (Stations.Num() == 0 || InStoreType == EStoreType::None)
	{
		return false;
	}

	const int32 CurrentIdx = FindStationArrayIndex(SelectedStationIndex);
	const int32 StartIdx = (CurrentIdx != INDEX_NONE) ? CurrentIdx : -1;

	for (int32 i = 1; i <= Stations.Num(); ++i)
	{
		const int32 TestIdx = (StartIdx + i) % Stations.Num();
		if (Stations[TestIdx].StoreType == InStoreType && IsAvailableStation(Stations[TestIdx]))
		{
			return SelectStation(Stations[TestIdx].StationIndex);
		}
	}

	return false;
}

bool UModularMissionManagement::CycleWeaponType()
{
	if (Stations.Num() == 0)
	{
		return false;
	}

	const EStoreType CurrentType = GetSelectedStoreType();

	// Find distinct available store types with ammo
	TArray<EStoreType> AvailableTypes;
	for (const FWeaponStation& Station : Stations)
	{
		if (IsAvailableStation(Station))
		{
			AvailableTypes.AddUnique(Station.StoreType);
		}
	}

	if (AvailableTypes.Num() == 0)
	{
		return false;
	}

	const int32 CurrentTypeIdx = AvailableTypes.Find(CurrentType);
	const int32 NextTypeIdx = (CurrentTypeIdx != INDEX_NONE) ? (CurrentTypeIdx + 1) % AvailableTypes.Num() : 0;

	return SelectStoreType(AvailableTypes[NextTypeIdx]);
}

bool UModularMissionManagement::SelectStoreType(EStoreType InStoreType)
{
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == InStoreType && IsAvailableStation(Station))
		{
			return SelectStation(Station.StationIndex);
		}
	}
	return false;
}

bool UModularMissionManagement::GetSelectedStation(FWeaponStation& OutStation) const
{
	return GetStation(SelectedStationIndex, OutStation);
}

bool UModularMissionManagement::GetActiveStore(FStationStore& OutStore) const
{
	return GetActiveStoreForStation(SelectedStationIndex, OutStore);
}

bool UModularMissionManagement::GetActiveStoreForStation(int32 StationIndex, FStationStore& OutStore) const
{
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex != INDEX_NONE)
	{
		const FWeaponStation& Station = Stations[ArrayIndex];
		for (const FStationStore& Store : Station.Stores)
		{
			if (Store.Status == EStoreStatus::Ready)
			{
				OutStore = Store;
				return true;
			}
		}

	}

	OutStore = FStationStore();
	return false;
}

EStoreType UModularMissionManagement::GetSelectedStoreType() const
{
	const int32 Index = FindStationArrayIndex(SelectedStationIndex);
	return Index != INDEX_NONE ? Stations[Index].StoreType : EStoreType::None;
}

AActor* UModularMissionManagement::GetActiveWeaponActor(int32 StationIndex) const
{
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex != INDEX_NONE)
	{
		for (const FStationStore& Store : Stations[ArrayIndex].Stores)
		{
			if (Store.Status == EStoreStatus::Ready)
			{
				return IsValid(Store.MountedActor) ? Store.MountedActor.Get() : nullptr;
			}
		}
	}
	return nullptr;
}

UMasterWeaponComponent* UModularMissionManagement::GetActiveWeaponComponent(int32 StationIndex) const
{
	if (AActor* WeaponActor = GetActiveWeaponActor(StationIndex))
	{
		return ResolveStoreWeapon(WeaponActor);
	}
	return nullptr;
}

void UModularMissionManagement::PrepareActiveWeaponOnStation(int32 StationIndex)
{
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		return;
	}

	if (MasterMode == EAircraftMasterMode::Navigation)
	{
		return;
	}

	UMasterWeaponComponent* ActiveWeapon = GetActiveWeaponComponent(StationIndex);
	if (!ActiveWeapon)
	{
		return;
	}

	ProgramWeaponIgnoreList(ActiveWeapon);
	ActiveWeapon->ActivateWeapon(true);
	UAircraftRadarComponent* Radar = ResolveRadarComponent();
	if (URadarMissileGuidanceComponent* RadarGuidance = Cast<URadarMissileGuidanceComponent>(ActiveWeapon))
	{
		if (Radar) RadarGuidance->SetParentRadar(Radar);
	}
	if (StationIndex != SelectedStationIndex)
	{
		PilotCuedWeapon.Reset();
		AutomaticallyCuedWeapon.Reset();
	}
	RefreshSelectedWeaponCue();
	if (AActor* CueTarget = GetCueTargetForWeapon(ActiveWeapon); IsValid(CueTarget) && Radar)
	{
		FRadarTrack Linked;
		if (Radar->GetSelectedLinkedTrackForActor(CueTarget, Linked))
		{
			PrepareLinkedRadarWeapon(Linked, Radar->GetLinkedTrackSource(Linked.TrackID));
		}
	}
}

void UModularMissionManagement::SetMasterArmMode(EMasterArmMode InMode)
{
	if (MasterArmMode != InMode)
	{
		TArray<int32> FiringStations;
		ActiveGunTimers.GetKeys(FiringStations);
		for (const int32 FiringStation : FiringStations) StopFiring(FiringStation);
		for (FWeaponStation& Station : Stations)
		{
			if (Station.StoreType == EStoreType::InternalCannon)
			{
				Station.SimulatedAmmo = Station.CurrentAmmo;
				OnStationAmmoChanged.Broadcast(Station.StationIndex, Station.CurrentAmmo, Station.MaxAmmo);
			}
		}
		MasterArmMode = InMode;
		if (MasterArmMode == EMasterArmMode::Safe)
		{
			StopFiring(SelectedStationIndex);
		}
		OnMasterArmChanged.Broadcast(MasterArmMode);

		if (UWorld* World = GetWorld(); World && World->IsGameWorld())
		{
			if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority())
			{
				ServerSetMasterArmMode(InMode);
			}
		}
	}
}

void UModularMissionManagement::SetMasterMode(EAircraftMasterMode InMode)
{
	if (MasterMode != InMode)
	{
		MasterMode = InMode;
		if (MasterMode == EAircraftMasterMode::Navigation)
		{
			StopFiring(SelectedStationIndex);
		}

		// Dynamically power up/down the selected weapon based on the new mode
		if (UMasterWeaponComponent* ActiveWeapon = GetActiveWeaponComponent(SelectedStationIndex))
		{
			if (MasterMode == EAircraftMasterMode::Navigation)
			{
				ActiveWeapon->ActivateWeapon(false);
			}
			else
			{
				ActiveWeapon->ActivateWeapon(true);
			}
		}

		if (bAutoSelectStationOnMasterMode)
		{
			switch (MasterMode)
			{
				case EAircraftMasterMode::Dogfight:
					SelectDogfightStation();
					break;
				case EAircraftMasterMode::MissileOverride:
					SelectBVRStation();
					break;
				default:
					break;
			}
		}

		OnMasterModeChanged.Broadcast(MasterMode);

		if (UWorld* World = GetWorld(); World && World->IsGameWorld())
		{
			if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority())
			{
				ServerSetMasterMode(InMode);
			}
		}
	}
}

AActor* UModularMissionManagement::GetLockedActor() const
{
	return IsValid(LockedTarget) ? LockedTarget.Get() : nullptr;
}

EWeaponComponentType UModularMissionManagement::GetSelectedWeaponComponentType() const
{
	const UMasterWeaponComponent* Weapon = GetActiveWeaponComponent(SelectedStationIndex);
	return Weapon ? Weapon->GetWeaponComponentType() : EWeaponComponentType::Unknown;
}

UMissileGuidanceComponent* UModularMissionManagement::GetSelectedMissileGuidance() const
{
	return Cast<UMissileGuidanceComponent>(GetActiveWeaponComponent(SelectedStationIndex));
}

UIRMissileGuidanceComponent* UModularMissionManagement::GetSelectedIRMissile() const
{
	return Cast<UIRMissileGuidanceComponent>(GetActiveWeaponComponent(SelectedStationIndex));
}

URadarMissileGuidanceComponent* UModularMissionManagement::GetSelectedRadarMissile() const
{
	return Cast<URadarMissileGuidanceComponent>(GetActiveWeaponComponent(SelectedStationIndex));
}

UARMMissileGuidanceComponent* UModularMissionManagement::GetSelectedARMMissile() const
{
	return Cast<UARMMissileGuidanceComponent>(GetActiveWeaponComponent(SelectedStationIndex));
}

UDroppableItemComponent* UModularMissionManagement::GetSelectedDroppableItem() const
{
	return Cast<UDroppableItemComponent>(GetActiveWeaponComponent(SelectedStationIndex));
}

void UModularMissionManagement::UnbindActiveWeaponDelegates()
{
	if (UMasterWeaponComponent* Weapon = SubscribedActiveWeapon.Get())
	{
		Weapon->OnSeekerStateChanged.RemoveDynamic(this, &UModularMissionManagement::HandleActiveWeaponSeekerStateChanged);
		Weapon->OnLockAcquired.RemoveDynamic(this, &UModularMissionManagement::HandleActiveWeaponLockAcquired);
		Weapon->OnLockLost.RemoveDynamic(this, &UModularMissionManagement::HandleActiveWeaponLockLost);
		Weapon->OnCageStateChanged.RemoveDynamic(this, &UModularMissionManagement::HandleActiveWeaponCageStateChanged);
	}
	SubscribedActiveWeapon.Reset();
}

void UModularMissionManagement::BindActiveWeaponDelegates()
{
	UMasterWeaponComponent* Weapon = GetActiveWeaponComponent(SelectedStationIndex);
	if (Weapon == SubscribedActiveWeapon.Get()) return;
	UnbindActiveWeaponDelegates();
	if (!IsValid(Weapon)) return;
	SubscribedActiveWeapon = Weapon;
	Weapon->OnSeekerStateChanged.AddUniqueDynamic(this, &UModularMissionManagement::HandleActiveWeaponSeekerStateChanged);
	Weapon->OnLockAcquired.AddUniqueDynamic(this, &UModularMissionManagement::HandleActiveWeaponLockAcquired);
	Weapon->OnLockLost.AddUniqueDynamic(this, &UModularMissionManagement::HandleActiveWeaponLockLost);
	Weapon->OnCageStateChanged.AddUniqueDynamic(this, &UModularMissionManagement::HandleActiveWeaponCageStateChanged);
}

void UModularMissionManagement::HandleActiveWeaponSeekerStateChanged(UMasterWeaponComponent* Weapon, EWeaponSeekerState OldState, EWeaponSeekerState NewState)
{
	if (IsValid(Weapon) && Weapon == SubscribedActiveWeapon.Get() && !Weapon->IsWeaponFired())
		OnActiveWeaponSeekerStateChanged.Broadcast(Weapon, OldState, NewState);
}

void UModularMissionManagement::HandleActiveWeaponLockAcquired(UMasterWeaponComponent* Weapon, AActor* Target)
{
	if (IsValid(Weapon) && Weapon == SubscribedActiveWeapon.Get() && !Weapon->IsWeaponFired())
		OnActiveWeaponLockAcquired.Broadcast(Weapon, Target);
}

void UModularMissionManagement::HandleActiveWeaponLockLost(UMasterWeaponComponent* Weapon, AActor* Target)
{
	if (IsValid(Weapon) && Weapon == SubscribedActiveWeapon.Get() && !Weapon->IsWeaponFired())
		OnActiveWeaponLockLost.Broadcast(Weapon, Target);
}

void UModularMissionManagement::HandleActiveWeaponCageStateChanged(UMasterWeaponComponent* Weapon, bool bCaged)
{
	if (IsValid(Weapon) && Weapon == SubscribedActiveWeapon.Get() && !Weapon->IsWeaponFired())
		OnActiveWeaponCageStateChanged.Broadcast(Weapon, bCaged);
}

bool UModularMissionManagement::SlaveSelectedWeaponToDirection(const FVector& InWorldDirection)
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && GetOwner() && !GetOwner()->HasAuthority())
		ServerSlaveSelectedWeaponToDirection(InWorldDirection);
	UMasterWeaponComponent* Weapon = GetActiveWeaponComponent(SelectedStationIndex);
	if (!Weapon || !Weapon->SlaveToDirection(InWorldDirection)) return false;
	PilotCuedWeapon = Weapon;
	AutomaticallyCuedWeapon.Reset();
	return true;
}

bool UModularMissionManagement::SlaveSelectedWeaponToLocation(const FVector& InWorldLocation)
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && GetOwner() && !GetOwner()->HasAuthority())
		ServerSlaveSelectedWeaponToLocation(InWorldLocation);
	UMasterWeaponComponent* Weapon = GetActiveWeaponComponent(SelectedStationIndex);
	if (!Weapon || !Weapon->SlaveToLocation(InWorldLocation)) return false;
	PilotCuedWeapon = Weapon;
	AutomaticallyCuedWeapon.Reset();
	return true;
}

void UModularMissionManagement::SlaveSelectedWeaponToBoresight()
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && GetOwner() && !GetOwner()->HasAuthority())
		ServerSlaveSelectedWeaponToBoresight();
	if (UMasterWeaponComponent* Weapon = GetActiveWeaponComponent(SelectedStationIndex))
	{
		Weapon->SlaveToBoresight();
		PilotCuedWeapon = Weapon;
		AutomaticallyCuedWeapon.Reset();
	}
}

void UModularMissionManagement::SetSelectedWeaponCaged(bool bCaged)
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && GetOwner() && !GetOwner()->HasAuthority())
		ServerSetSelectedWeaponCaged(bCaged);
	if (UMasterWeaponComponent* Weapon = GetActiveWeaponComponent(SelectedStationIndex))
	{
		Weapon->SetSeekerCaged(bCaged);
		PilotCuedWeapon = Weapon;
		AutomaticallyCuedWeapon.Reset();
	}
}

AActor* UModularMissionManagement::GetCueTargetForWeapon(const UMasterWeaponComponent* Weapon) const
{
	if (!Weapon) return nullptr;
	if (Weapon->FiringRequirement == EWeaponFiringRequirement::HardLock)
	{
		AActor* Target = GetLockedActor();
		if (Cast<UHybridRadarMissileGuidanceComponent>(Weapon) && IsValid(Target))
		{
			UAircraftRadarComponent* Radar = GetRadarComponent();
			if (Radar && Radar->IsContinuousWaveIlluminating(Target)) return Target;
			FRadarTrack Linked;
			if (Radar && Radar->GetSelectedLinkedTrackForActor(Target, Linked))
				if (UAircraftRadarComponent* Source = Radar->GetLinkedTrackSource(Linked.TrackID);
					IsValid(Source) && Source->IsContinuousWaveIlluminating(Target)) return Target;
			return nullptr;
		}
		return Target;
	}
	return GetBuggedActor() ? GetBuggedActor() : GetLockedActor();
}

bool UModularMissionManagement::IsWeaponReleaseInhibitedByIFF(
	const UMasterWeaponComponent* Weapon, const AActor* CueTarget) const
{
	if (!bInhibitFriendlyFire) return false;
	if (IsValid(CueTarget) && FCombatTeamUtility::IsFriendly(GetOwner(), CueTarget)) return true;
	const UIRMissileGuidanceComponent* IR = Cast<UIRMissileGuidanceComponent>(Weapon);
	const AActor* SeekerTarget = IR ? IR->GetLockedTarget() : nullptr;
	return IsValid(SeekerTarget) && FCombatTeamUtility::IsFriendly(GetOwner(), SeekerTarget);
}

void UModularMissionManagement::RefreshSelectedWeaponCue()
{
	const int32 ArrayIndex = FindStationArrayIndex(SelectedStationIndex);
	if (ArrayIndex == INDEX_NONE) return;
	for (const FStationStore& Store : Stations[ArrayIndex].Stores)
	{
		if (Store.Status != EStoreStatus::Ready || !IsValid(Store.MountedActor)) continue;
		UMissileGuidanceComponent* Guidance = Cast<UMissileGuidanceComponent>(ResolveStoreWeapon(Store.MountedActor));
		if (!Guidance) break;
		if (PilotCuedWeapon.Get() == Guidance) break;
		AActor* CueTarget = GetCueTargetForWeapon(Guidance);
		if (UIRMissileGuidanceComponent* IR = Cast<UIRMissileGuidanceComponent>(Guidance))
		{
			if (IsValid(CueTarget) && IR->SlaveToTarget(CueTarget))
				AutomaticallyCuedWeapon = Guidance;
			else if (AutomaticallyCuedWeapon.Get() == Guidance)
			{
				IR->SlaveToBoresight();
				AutomaticallyCuedWeapon.Reset();
			}
		}
		else if (URadarMissileGuidanceComponent* Missile = Cast<URadarMissileGuidanceComponent>(Guidance))
		{
			bool bAppliedRadarCue = false;
			if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
			{
				Missile->SetParentRadar(Radar);
				FRadarTrack Track;
				if (IsValid(CueTarget) && Missile->SlaveToTarget(CueTarget))
				{
					bAppliedRadarCue = true;
					AutomaticallyCuedWeapon = Guidance;
					if (Radar->GetTrackByActor(CueTarget, Track))
						Missile->SetInertialTarget(Track.LastKnownPosition, Track.EstimatedVelocity);
				}
			}
			if (!bAppliedRadarCue && AutomaticallyCuedWeapon.Get() == Guidance)
			{
				Missile->SlaveToBoresight();
				AutomaticallyCuedWeapon.Reset();
			}
		}
		else if (UARMMissileGuidanceComponent* ARM = Cast<UARMMissileGuidanceComponent>(Guidance))
		{
			UAircraftRadarComponent* Emitter = IsValid(CueTarget) ?
				CueTarget->FindComponentByClass<UAircraftRadarComponent>() : nullptr;
			if (IsValid(Emitter) && Emitter->IsRadarEmitting())
			{
				ARM->HandoffEmitter(CueTarget, Emitter);
				AutomaticallyCuedWeapon = Guidance;
			}
			else if (AutomaticallyCuedWeapon.Get() == Guidance)
			{
				ARM->HandoffEmitter(nullptr);
				AutomaticallyCuedWeapon.Reset();
			}
		}
		break;
	}
}

void UModularMissionManagement::LockActor(AActor* InTarget)
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
		if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority())
		{
			ServerLockActor(InTarget);
			return;
		}
	// Validate target
	AActor* ValidNewTarget = IsValid(InTarget) ? InTarget : nullptr;

	// Ignore if already tracking this target
	if (LockedTarget == ValidNewTarget)
	{
		return;
	}
	// Unbind previous target delegates
	if (IsValid(LockedTarget))
	{
		LockedTarget->OnDestroyed.RemoveDynamic(this, &UModularMissionManagement::HandleCueTargetDestroyed);
	}

	LockedTarget = ValidNewTarget;
	PilotCuedWeapon.Reset();
	if (IsValid(BuggedTarget))
		BuggedTarget->OnDestroyed.AddUniqueDynamic(this, &UModularMissionManagement::HandleCueTargetDestroyed);

	if (LockedTarget)
	{
		LockedTarget->OnDestroyed.AddUniqueDynamic(this, &UModularMissionManagement::HandleCueTargetDestroyed);
		OnTargetLocked.Broadcast(LockedTarget);
	}
	else
	{
		OnLockCleared.Broadcast();
	}



	RefreshSelectedWeaponCue();

}

void UModularMissionManagement::PrepareLinkedRadarWeapon(const FRadarTrack& LinkedTrack,
	UAircraftRadarComponent* SourceRadar)
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
		if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (!IsValid(SourceRadar)) return;
	UAircraftRadarComponent* LauncherRadar = ResolveRadarComponent();
	if (!LauncherRadar || !LauncherRadar->bAllowRemoteWeaponSupport) return;
	if (URadarMissileGuidanceComponent* Missile = Cast<URadarMissileGuidanceComponent>(
		GetActiveWeaponComponent(SelectedStationIndex)))
	{
		const bool bHardLock = Missile->FiringRequirement == EWeaponFiringRequirement::HardLock;
		if (bHardLock && (!IsValid(LockedTarget) ||
			!SourceRadar->IsContinuousWaveIlluminating(LockedTarget.Get()))) return;
		PilotCuedWeapon.Reset();
		Missile->SetRemoteDataLinkSupport(LauncherRadar, SourceRadar, LinkedTrack.SourceParticipantID);
		if (AActor* LinkedTarget = LinkedTrack.TrackedActor.Get(); IsValid(LinkedTarget))
			Missile->SlaveToTarget(LinkedTarget);
		Missile->SetInertialTarget(LinkedTrack.LastKnownPosition +
			LinkedTrack.EstimatedVelocity * LinkedTrack.TrackAge, LinkedTrack.EstimatedVelocity);
		AutomaticallyCuedWeapon = Missile;
	}
}

void UModularMissionManagement::ClearLockedActor()
{
	LockActor(nullptr);
}

void UModularMissionManagement::BugActor(AActor* InTarget)
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
		if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority())
		{
			ServerBugActor(InTarget);
			return;
		}
	AActor* ValidTarget = IsValid(InTarget) ? InTarget : nullptr;
	if (BuggedTarget == ValidTarget) return;
	if (IsValid(BuggedTarget))
		BuggedTarget->OnDestroyed.RemoveDynamic(this, &UModularMissionManagement::HandleCueTargetDestroyed);
	BuggedTarget = ValidTarget;
	PilotCuedWeapon.Reset();
	if (IsValid(LockedTarget))
		LockedTarget->OnDestroyed.AddUniqueDynamic(this, &UModularMissionManagement::HandleCueTargetDestroyed);
	if (BuggedTarget)
	{
		BuggedTarget->OnDestroyed.AddUniqueDynamic(this, &UModularMissionManagement::HandleCueTargetDestroyed);
		OnTargetBugged.Broadcast(BuggedTarget);
	}
	else OnBugCleared.Broadcast();
	RefreshSelectedWeaponCue();
}

void UModularMissionManagement::ClearBuggedActor()
{
	BugActor(nullptr);
}

void UModularMissionManagement::HandleRadarLockAcquired(const FRadarTrack& Track)
{
	LockActor(Track.TrackedActor.Get());
}

void UModularMissionManagement::HandleRadarLockLost(int32 TrackID)
{
	(void)TrackID;
	if (UAircraftRadarComponent* Radar = GetRadarComponent())
		LockActor(Radar->GetSTTLockedActor());
	else ClearLockedActor();
}

void UModularMissionManagement::HandleRadarTrackSelected(const FRadarTrack& Track)
{
	if (UAircraftRadarComponent* Radar = GetRadarComponent();
		Radar && Track.TrackID == Radar->GetBuggedTrackID())
		BugActor(Track.TrackedActor.Get());
}

void UModularMissionManagement::HandleRadarTrackDeselected(int32 TrackID)
{
	(void)TrackID;
	if (UAircraftRadarComponent* Radar = GetRadarComponent())
	{
		FRadarTrack Bug;
		BugActor(Radar->GetBuggedTrackID() >= 0 &&
			Radar->GetTrackByID(Radar->GetBuggedTrackID(), Bug) ? Bug.TrackedActor.Get() : nullptr);
	}
}

UAircraftRadarComponent* UModularMissionManagement::ResolveRadarComponent()
{
	if (!IsValid(CachedRadarComponent))
	{
		if (AActor* OwnerActor = GetOwner())
		{
			CachedRadarComponent = OwnerActor->FindComponentByClass<UAircraftRadarComponent>();
			if (IsValid(CachedRadarComponent) && HasBegunPlay() && HasInventoryAuthority(this))
			{
				CachedRadarComponent->OnRadarLockAcquired.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarLockAcquired);
				CachedRadarComponent->OnRadarLockLost.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarLockLost);
				CachedRadarComponent->OnRadarTrackSelected.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarTrackSelected);
				CachedRadarComponent->OnRadarTrackDeselected.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarTrackDeselected);
				LockActor(CachedRadarComponent->GetSTTLockedActor());
				FRadarTrack Bug;
				BugActor(CachedRadarComponent->GetTrackByID(CachedRadarComponent->GetBuggedTrackID(), Bug) ? Bug.TrackedActor.Get() : nullptr);
			}
		}
	}
	return CachedRadarComponent;
}

UAircraftRadarComponent* UModularMissionManagement::GetRadarComponent() const
{
	if (IsValid(CachedRadarComponent))
	{
		return CachedRadarComponent;
	}

	if (AActor* OwnerActor = GetOwner())
	{
		return OwnerActor->FindComponentByClass<UAircraftRadarComponent>();
	}

	return nullptr;
}

URadarWarningReceiverComponent* UModularMissionManagement::ResolveRWRComponent()
{
	if (!IsValid(CachedRWRComponent))
	{
		if (AActor* OwnerActor = GetOwner())
		{
			CachedRWRComponent = OwnerActor->FindComponentByClass<URadarWarningReceiverComponent>();
		}
	}
	return CachedRWRComponent;
}

URadarWarningReceiverComponent* UModularMissionManagement::GetRWRComponent() const
{
	if (IsValid(CachedRWRComponent))
	{
		return CachedRWRComponent;
	}

	if (AActor* OwnerActor = GetOwner())
	{
		return OwnerActor->FindComponentByClass<URadarWarningReceiverComponent>();
	}

	return nullptr;
}

bool UModularMissionManagement::CalculateMissileLaunchZone(int32 StationIndex, float& OutRmin, float& OutRne, float& OutRmax, bool& OutInShootingEnvelope) const
{
	OutRmin = 0.0f;
	OutRne = 0.0f;
	OutRmax = 0.0f;
	OutInShootingEnvelope = false;
	UMasterWeaponComponent* Weapon = GetActiveWeaponComponent(StationIndex);
	AActor* Target = GetCueTargetForWeapon(Weapon);
	if (const UIRMissileGuidanceComponent* IR = Cast<UIRMissileGuidanceComponent>(Weapon);
		IR && IsValid(IR->GetLockedTarget())) Target = IR->GetLockedTarget();
	if (!Weapon || !IsValid(Target) || !Weapon->GetDynamicLaunchZone(Target, OutRmin, OutRne, OutRmax)) return false;
	OutInShootingEnvelope = Weapon->IsTargetInLaunchEnvelope(Target);
	return true;
}

bool UModularMissionManagement::GetPrimaryRadarTarget(FRadarTrack& OutTrack) const
{
	if (UAircraftRadarComponent* Radar = GetRadarComponent())
	{
		const int32 STTID = Radar->GetSTTLockedTrackID();
		if (STTID != INDEX_NONE && Radar->GetTrackByID(STTID, OutTrack))
		{
			return true;
		}

		const int32 BugID = Radar->GetBuggedTrackID();
		if (BugID != INDEX_NONE && Radar->GetTrackByID(BugID, OutTrack))
		{
			return true;
		}

		if (IsValid(BuggedTarget))
		{
			if (Radar->GetTrackByActor(BuggedTarget.Get(), OutTrack)) return true;
		}
		if (IsValid(LockedTarget))
		{
			if (Radar->GetTrackByActor(LockedTarget.Get(), OutTrack))
			{
				return true;
			}
		}
	}
	OutTrack = FRadarTrack();
	return false;
}

bool UModularMissionManagement::GetGunLeadSolution(int32 StationIndex, FVector& OutLeadLocation, float& OutTimeOfFlight) const
{
	OutLeadLocation = FVector::ZeroVector;
	OutTimeOfFlight = 0.0f;

	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}

	const FWeaponStation& Station = Stations[ArrayIndex];
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || (Station.StoreType != EStoreType::InternalCannon && Station.StoreType != EStoreType::GunPod))
	{
		return false;
	}

	// Retrieve target kinematics from radar track or locked target
	FVector TargetPos = FVector::ZeroVector;
	FVector TargetVel = FVector::ZeroVector;
	FRadarTrack TargetTrack;

	if (GetPrimaryRadarTarget(TargetTrack))
	{
		TargetPos = TargetTrack.LastKnownPosition;
		TargetVel = TargetTrack.EstimatedVelocity;
	}
	else if (AActor* CueTarget = IsValid(BuggedTarget) ? BuggedTarget.Get() : GetLockedActor())
	{
		TargetPos = CueTarget->GetActorLocation();
		TargetVel = CueTarget->GetVelocity();
	}
	else
	{
		return false;
	}

	// Determine muzzle exit position and bullet speed
	FVector MuzzlePos = OwnerActor->GetActorLocation();
	float BulletSpeed = 105000.0f; // 1050 m/s default (~20mm M61 Vulcan)
	float GravityScale = 1.0f;

	if (Station.StoreType == EStoreType::InternalCannon && Station.GunMuzzles.Num() > 0)
	{
		const FGunMuzzle& Muzzle = Station.GunMuzzles[0];
		BulletSpeed = Muzzle.BulletSpeed;
		GravityScale = Muzzle.GravityScale;
		if (USceneComponent* MeshComp = ResolveAircraftMesh())
		{
			if (Muzzle.MuzzleSocketName != NAME_None && MeshComp->DoesSocketExist(Muzzle.MuzzleSocketName))
			{
				const FTransform MuzzleTransform = MeshComp->GetSocketTransform(Muzzle.MuzzleSocketName);
				MuzzlePos = MuzzleTransform.GetLocation();
			}
		}
	}
	else
	{
		FStationStore ActiveStore;
		if (GetActiveStoreForStation(StationIndex, ActiveStore))
		{
			if (USceneComponent* MeshComp = ResolveAircraftMesh())
			{
				if (ActiveStore.SocketName != NAME_None && MeshComp->DoesSocketExist(ActiveStore.SocketName))
				{
					const FTransform StoreTransform = MeshComp->GetSocketTransform(ActiveStore.SocketName);
					MuzzlePos = StoreTransform.GetLocation();
				}
			}
		}
	}

	const float Distance = FVector::Dist(MuzzlePos, TargetPos);
	if (Distance <= KINDA_SMALL_NUMBER || BulletSpeed <= 0.0f)
	{
		return false;
	}

	const FVector AircraftVel = OwnerActor->GetVelocity();
	const FVector RelativeVel = TargetVel - AircraftVel;
	const FVector Offset = TargetPos - MuzzlePos;
	const double A = RelativeVel.SizeSquared() - FMath::Square(static_cast<double>(BulletSpeed));
	const double B = 2.0 * FVector::DotProduct(Offset, RelativeVel);
	const double C = Offset.SizeSquared();
	double TOF = -1.0;
	if (FMath::Abs(A) < 1.e-6)
	{
		if (B < -SMALL_NUMBER) TOF = -C / B;
	}
	else
	{
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant >= 0.0)
		{
			const double Root = FMath::Sqrt(Discriminant);
			const double T1 = (-B - Root) / (2.0 * A);
			const double T2 = (-B + Root) / (2.0 * A);
			if (T1 > 0.0) TOF = T1;
			if (T2 > 0.0 && (TOF <= 0.0 || T2 < TOF)) TOF = T2;
		}
	}
	if (!FMath::IsFinite(TOF) || TOF <= 0.0) return false;

	// Gravity compensation (Z-drop)
	const float GravityZ = GetWorld() ? GetWorld()->GetGravityZ() : -980.665f;
	const FVector BulletGravityDrop(0.0f, 0.0f, 0.5f * GravityZ * GravityScale * TOF * TOF);

	// Future predicted target position
	const FVector FutureTargetPos = TargetPos + (TargetVel * TOF);

	// Commanded aim point for lead pipper
	OutLeadLocation = FutureTargetPos - BulletGravityDrop - (AircraftVel * TOF);
	OutTimeOfFlight = TOF;
	return true;
}



bool UModularMissionManagement::SelectDogfightStation()
{
	// Priority 1: Internal Cannon
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::InternalCannon && IsAvailableStation(Station))
		{
			return SelectStation(Station.StationIndex);
		}
	}

	// Priority 2: Heat-seeking short-range IR missile (Sidewinder / Archer)
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::AirToAirMissile_IR && IsAvailableStation(Station))
		{
			return SelectStation(Station.StationIndex);
		}
	}

	// Priority 3: Gun Pod
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::GunPod && IsAvailableStation(Station))
		{
			return SelectStation(Station.StationIndex);
		}
	}

	return false;
}

bool UModularMissionManagement::SelectBVRStation()
{
	// Priority 1: Radar-guided medium/long range missile (AMRAAM / Sparrow)
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::AirToAirMissile_Radar && IsAvailableStation(Station))
		{
			return SelectStation(Station.StationIndex);
		}
	}

	// Priority 2: IR missile fallback
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::AirToAirMissile_IR && IsAvailableStation(Station))
		{
			return SelectStation(Station.StationIndex);
		}
	}

	return false;
}

void UModularMissionManagement::HandleCueTargetDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == LockedTarget) ClearLockedActor();
	if (DestroyedActor == BuggedTarget) ClearBuggedActor();
}

bool UModularMissionManagement::CanFire(int32 StationIndex, EWeaponLaunchFailureReason& OutFailReason) const
{
	if (bStoreReleaseInProgress)
	{
		OutFailReason = EWeaponLaunchFailureReason::WeaponNotReady;
		return false;
	}
	if (MasterArmMode == EMasterArmMode::Safe)
	{
		OutFailReason = EWeaponLaunchFailureReason::MasterArmSafe;
		return false;
	}

	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		OutFailReason = EWeaponLaunchFailureReason::StationNotFound;
		return false;
	}

	if (MasterMode == EAircraftMasterMode::Navigation)
	{
		OutFailReason = EWeaponLaunchFailureReason::WeaponNotReady;
		return false;
	}
	const FWeaponStation& Station = Stations[ArrayIndex];
	const UMasterWeaponComponent* SelectedWeapon = GetActiveWeaponComponent(StationIndex);
	AActor* CueTarget = Station.StoreType == EStoreType::InternalCannon ?
		(GetBuggedActor() ? GetBuggedActor() : GetLockedActor()) : GetCueTargetForWeapon(SelectedWeapon);
	if (IsValid(CueTarget) && Cast<URadarMissileGuidanceComponent>(SelectedWeapon))
	{
		if (UAircraftRadarComponent* Radar = GetRadarComponent())
		{
			FRadarTrack Linked;
			if (Radar->GetSelectedLinkedTrackID() != -1)
			{
				if (!Radar->GetSelectedLinkedTrackForActor(CueTarget, Linked))
				{
					OutFailReason = EWeaponLaunchFailureReason::DatalinkSupportUnavailable;
					return false;
				}
				UAircraftRadarComponent* Source = Radar->GetLinkedTrackSource(Linked.TrackID);
				if (!Radar->bAllowRemoteWeaponSupport || !IsValid(Source))
				{
					OutFailReason = EWeaponLaunchFailureReason::DatalinkSupportUnavailable;
					return false;
				}
				if (Station.StoreType != EStoreType::AirToAirMissile_Radar && Station.StoreType != EStoreType::AirToGroundMissile)
				{
					OutFailReason = EWeaponLaunchFailureReason::InvalidStoreType;
					return false;
				}
				const URadarMissileGuidanceComponent* Missile = Cast<URadarMissileGuidanceComponent>(
					GetActiveWeaponComponent(StationIndex));
				if (!Missile)
				{
					OutFailReason = EWeaponLaunchFailureReason::NoWeaponConfigured;
					return false;
				}
				if ((Cast<USemiActiveRadarMissileGuidanceComponent>(Missile) ||
					Cast<UHybridRadarMissileGuidanceComponent>(Missile)) &&
					!Source->IsContinuousWaveIlluminating(CueTarget))
				{
					OutFailReason = EWeaponLaunchFailureReason::NoContinuousWaveIllumination;
					return false;
				}
			}
			else if ((Cast<USemiActiveRadarMissileGuidanceComponent>(SelectedWeapon) ||
				(Cast<UHybridRadarMissileGuidanceComponent>(SelectedWeapon) &&
				 SelectedWeapon->FiringRequirement == EWeaponFiringRequirement::HardLock)) &&
				!Radar->IsContinuousWaveIlluminating(CueTarget))
			{
				// A retained STT track is insufficient when an animated plate is pointing away.
				OutFailReason = EWeaponLaunchFailureReason::NoContinuousWaveIllumination;
				return false;
			}
		}
	}

	if (Station.StoreType == EStoreType::None)
	{
		OutFailReason = EWeaponLaunchFailureReason::NoWeaponConfigured;
		return false;
	}

	if (Station.StoreType == EStoreType::InternalCannon)
	{
		if (Station.GunMuzzles.Num() == 0)
		{
			OutFailReason = EWeaponLaunchFailureReason::NoWeaponConfigured;
			return false;
		}
	}
	else
	{
		if (Station.Stores.Num() == 0)
		{
			OutFailReason = EWeaponLaunchFailureReason::NoWeaponConfigured;
			return false;
		}
	}

	const int32 AvailableAmmo = Station.StoreType == EStoreType::InternalCannon && MasterArmMode == EMasterArmMode::Simulate
		? Station.SimulatedAmmo : Station.GetCurrentAmmo();
	if (AvailableAmmo <= 0 || Station.Status == EStationStatus::Empty)
	{
		OutFailReason = EWeaponLaunchFailureReason::AmmoDepleted;
		return false;
	}

	if (Station.Status == EStationStatus::Fault)
	{
		OutFailReason = EWeaponLaunchFailureReason::StationFault;
		return false;
	}

	if (Station.Status == EStationStatus::Jettisoned)
	{
		OutFailReason = EWeaponLaunchFailureReason::StationJettisoned;
		return false;
	}

	if (Station.StoreType != EStoreType::InternalCannon)
	{
		FStationStore Store;
		if (!GetActiveStoreForStation(StationIndex, Store) || Store.Status != EStoreStatus::Ready)
		{
			OutFailReason = EWeaponLaunchFailureReason::AmmoDepleted;
			return false;
		}
		if (!IsValid(Store.MountedActor))
		{
			if (!Store.WeaponClass)
			{
				OutFailReason = EWeaponLaunchFailureReason::NoWeaponConfigured;
				return false;
			}
			// On-demand actors are checked again before any physical release.
		}
		else
		{
			TInlineComponentArray<UMasterWeaponComponent*> Weapons(Store.MountedActor.Get());
			if (Weapons.Num() > 1 || !HasRequiredWeaponComponent(Station.StoreType, SelectedWeapon) ||
				(Station.bUsesAmmoManagement && (!SelectedWeapon ||
				 Cast<UMissileGuidanceComponent>(SelectedWeapon) || Cast<UDroppableItemComponent>(SelectedWeapon))))
			{
				OutFailReason = EWeaponLaunchFailureReason::NoWeaponConfigured;
				return false;
			}
			if (SelectedWeapon && !SelectedWeapon->CanFireWeapon(OutFailReason)) return false;
		}
	}

	// The IR head can acquire independently of the aircraft's radar designation.
	if (IsWeaponReleaseInhibitedByIFF(SelectedWeapon, CueTarget))
	{
		OutFailReason = EWeaponLaunchFailureReason::FriendlyTargetInhibit;
		return false;
	}

	OutFailReason = EWeaponLaunchFailureReason::None;
	return true;
}

bool UModularMissionManagement::Fire(int32 StationIndex)
{
	AActor* OwnerActor = GetOwner();
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && OwnerActor && !OwnerActor->HasAuthority())
	{
		ServerFire(StationIndex);
		return true; // Request queued; OnWeaponLaunchResult reports the server result.
	}

	EWeaponLaunchFailureReason FailReason = EWeaponLaunchFailureReason::None;
	if (!CanFire(StationIndex, FailReason))
	{
		LastLaunchFailureReason = FailReason;
		OnWeaponLaunchFailed.Broadcast(StationIndex, FailReason);
		return false;
	}

	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		LastLaunchFailureReason = EWeaponLaunchFailureReason::StationNotFound;
		OnWeaponLaunchFailed.Broadcast(StationIndex, LastLaunchFailureReason);
		return false;
	}

	FWeaponStation& Station = Stations[ArrayIndex];
	AActor* TargetToUse = GetCueTargetForWeapon(GetActiveWeaponComponent(StationIndex));

	// Internal cannon single burst cycle
	if (Station.StoreType == EStoreType::InternalCannon)
	{
		if (!ProcessGunFireCycle(StationIndex))
		{
			return false;
		}
		LastLaunchFailureReason = EWeaponLaunchFailureReason::None;
		OnWeaponFired.Broadcast(StationIndex, nullptr, Station.StoreType,
			MasterArmMode == EMasterArmMode::Simulate ? Station.SimulatedAmmo : Station.CurrentAmmo);
		return true;
	}

	// Dry fire simulation
	if (MasterArmMode == EMasterArmMode::Simulate)
	{
		LastLaunchFailureReason = EWeaponLaunchFailureReason::None;
		OnWeaponFired.Broadcast(StationIndex, nullptr, Station.StoreType, Station.GetCurrentAmmo());
		return true;
	}

	// Live Release
	TGuardValue<bool> ReleaseGuard(bStoreReleaseInProgress, true);
	EWeaponLaunchFailureReason ReleaseFailReason = EWeaponLaunchFailureReason::None;
	if (SelectedStationIndex == StationIndex) UnbindActiveWeaponDelegates();
	AActor* ReleasedActor = ExecuteStoreRelease(Station, TargetToUse, ReleaseFailReason);
	if (!IsValid(ReleasedActor))
	{
		BindActiveWeaponDelegates();
		LastLaunchFailureReason = (ReleaseFailReason != EWeaponLaunchFailureReason::None) ? ReleaseFailReason : EWeaponLaunchFailureReason::ReleaseMechanismFailed;
		OnWeaponLaunchFailed.Broadcast(StationIndex, LastLaunchFailureReason);
		return false;
	}

	LastLaunchFailureReason = EWeaponLaunchFailureReason::None;

	if (Station.bUsesAmmoManagement) Station.CurrentAmmo = FMath::Max(0, Station.CurrentAmmo - 1);
	const int32 RemainingAmmo = Station.GetCurrentAmmo();
	const int32 Capacity = Station.GetMaxAmmo();
	const EStoreType FiredType = Station.StoreType;
	const int32 SymmetricStation = Station.SymmetricStationIndex;
	if (RemainingAmmo == 0) Station.Status = EStationStatus::Empty;
	OnStationAmmoChanged.Broadcast(StationIndex, RemainingAmmo, Capacity);
	if (RemainingAmmo == 0) OnStationStatusChanged.Broadcast(StationIndex, EStationStatus::Empty);

	// Report the actual release before auto-stepping to its symmetric partner (e.g. 7 -> 5).
	OnWeaponFired.Broadcast(StationIndex, ReleasedActor, FiredType, RemainingAmmo);

	// Auto-step station logic to maintain balance
	if (bAutoStepStationOnFire && SelectedStationIndex == StationIndex)
	{
		if (SymmetricStation > 0)
		{
			FWeaponStation SymStation;
			if (GetStation(SymmetricStation, SymStation) &&
				SymStation.StoreType == FiredType && IsAvailableStation(SymStation))
			{
				SelectStation(SymmetricStation);
			}
			else
			{
				SelectNextStationOfStoreType(FiredType);
			}
		}
		else
		{
			SelectNextStationOfStoreType(FiredType);
		}
	}
	else if (SelectedStationIndex == StationIndex)
	{
		// If we didn't change stations, prepare the next weapon on this same rack.
		PrepareActiveWeaponOnStation(SelectedStationIndex);
	}

	OnStoresInventoryChanged.Broadcast();
	BindActiveWeaponDelegates();

	return true;
}

bool UModularMissionManagement::FireMountedWeapon(const UMasterWeaponComponent* Weapon)
{
	if (!IsValid(Weapon) || !IsValid(Weapon->GetOwner())) return false;
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::InternalCannon) continue;
		if (GetActiveWeaponComponent(Station.StationIndex) == Weapon)
		{
			return Fire(Station.StationIndex);
		}
	}
	return false;
}

void UModularMissionManagement::StartFiring(int32 StationIndex)
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority())
		{
			ServerStartFiring(StationIndex);
			return;
		}
	}
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		OnWeaponLaunchFailed.Broadcast(StationIndex, EWeaponLaunchFailureReason::StationNotFound);
		return;
	}

	FWeaponStation& Station = Stations[ArrayIndex];
	if (Station.StoreType != EStoreType::InternalCannon)
	{
		UE_LOG(LogTemp, Warning, TEXT("UModularMissionManagement::StartFiring: Station %d is not configured as an InternalCannon! Aborting."), StationIndex);
		return;
	}

	if (ActiveGunTimers.Contains(StationIndex))
	{
		return;
	}

	EWeaponLaunchFailureReason FailReason;
	if (!CanFire(StationIndex, FailReason))
	{
		OnWeaponLaunchFailed.Broadcast(StationIndex, FailReason);
		return;
	}

	const float SafeRPM = FMath::Max(60.0f, Station.RateOfFireRPM);
	const float FireInterval = 60.0f / SafeRPM;

	// Broadcast trigger start
	OnGunFiringStarted.Broadcast(StationIndex);

	if (!ProcessGunFireCycle(StationIndex))
	{
		OnGunFiringStopped.Broadcast(StationIndex);
		return;
	}

	// Recurring timer for continuous fire
	FWeaponStation CurrentStation;
	EWeaponLaunchFailureReason CurrentReason;
	if (GetStation(StationIndex, CurrentStation) && CanFire(StationIndex, CurrentReason))
	{
		FTimerDelegate TimerDel;
		TimerDel.BindUObject(this, &UModularMissionManagement::HandleGunFireTimerTick, StationIndex);

		FTimerHandle NewHandle;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(NewHandle, TimerDel, FireInterval, true);
			ActiveGunTimers.Add(StationIndex, NewHandle);
		}
	}
	else
	{
		OnGunFiringStopped.Broadcast(StationIndex);
	}

	GunFiringState.StationIndex = StationIndex;
	GunFiringState.bIsFiring = ActiveGunTimers.Contains(StationIndex);
}

void UModularMissionManagement::StopFiring(int32 StationIndex)
{
	if (FTimerHandle* HandlePtr = ActiveGunTimers.Find(StationIndex))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(*HandlePtr);
		}
		ActiveGunTimers.Remove(StationIndex);
		OnGunFiringStopped.Broadcast(StationIndex);
	}

	AActor* OwnerActor = GetOwner();
	const bool bHasAuthority = !GetWorld() || !GetWorld()->IsGameWorld() || !OwnerActor || OwnerActor->HasAuthority();

	if (!bHasAuthority)
	{
		ServerStopFiring(StationIndex);
	}
	else
	{
		if (GunFiringState.StationIndex == StationIndex)
		{
			GunFiringState.bIsFiring = false;
		}
	}
}

void UModularMissionManagement::StartFiringActive()
{
	StartFiring(SelectedStationIndex);
}

void UModularMissionManagement::StopFiringActive()
{
	StopFiring(SelectedStationIndex);
}

bool UModularMissionManagement::IsStationFiring(int32 StationIndex) const
{
	return ActiveGunTimers.Contains(StationIndex) ||
		(GunFiringState.bIsFiring && GunFiringState.StationIndex == StationIndex);
}

bool UModularMissionManagement::IsAnyGunFiring() const
{
	return ActiveGunTimers.Num() > 0 || GunFiringState.bIsFiring;
}

bool UModularMissionManagement::ProcessGunFireCycle(int32 StationIndex)
{
	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		if (AActor* OwnerActor = GetOwner(); OwnerActor && !OwnerActor->HasAuthority()) return false;
	}
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		StopFiring(StationIndex);
		return false;
	}

	FWeaponStation& Station = Stations[ArrayIndex];
	if (Station.StoreType != EStoreType::InternalCannon)
	{
		StopFiring(StationIndex);
		return false;
	}

	EWeaponLaunchFailureReason GateReason;
	if (!CanFire(StationIndex, GateReason))
	{
		StopFiring(StationIndex);
		LastLaunchFailureReason = GateReason;
		OnWeaponLaunchFailed.Broadcast(StationIndex, GateReason);
		return false;
	}

	const bool bSimulatedFire = MasterArmMode == EMasterArmMode::Simulate;
	if ((bSimulatedFire ? Station.SimulatedAmmo : Station.CurrentAmmo) <= 0 || Station.GunMuzzles.Num() == 0)
	{
		StopFiring(StationIndex);
		if (!bSimulatedFire) Station.Status = EStationStatus::Empty;
		LastLaunchFailureReason = EWeaponLaunchFailureReason::AmmoDepleted;
		OnWeaponLaunchFailed.Broadcast(StationIndex, LastLaunchFailureReason);
		if (!bSimulatedFire)
		{
			OnStationStatusChanged.Broadcast(StationIndex, EStationStatus::Empty);
			OnStoresInventoryChanged.Broadcast();
		}
		return false;
	}

	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		StopFiring(StationIndex);
		return false;
	}

	USceneComponent* MeshComp = ResolveAircraftMesh();
	const FVector AircraftVelocity = OwnerActor->GetVelocity();

	// Determine which muzzles to fire this cycle
	TArray<int32> MuzzlesToFire;
	if (Station.GunFiringPattern == EGunFiringPattern::Simultaneous)
	{
		for (int32 i = 0; i < Station.GunMuzzles.Num(); ++i)
		{
			MuzzlesToFire.Add(i);
		}
	}
	else // Alternating
	{
		int32& MuzzleIdx = GunMuzzleIndices.FindOrAdd(StationIndex, 0);
		if (Station.GunMuzzles.Num() > 0)
		{
			const int32 CandidateIdx = MuzzleIdx % Station.GunMuzzles.Num();
			MuzzlesToFire.Add(CandidateIdx);
			MuzzleIdx = (CandidateIdx + 1) % Station.GunMuzzles.Num();
		}
	}

	if (MuzzlesToFire.Num() == 0)
	{
		StopFiring(StationIndex);
		LastLaunchFailureReason = EWeaponLaunchFailureReason::NoWeaponConfigured;
		OnWeaponLaunchFailed.Broadcast(StationIndex, LastLaunchFailureReason);
		return false;
	}

	int32 RoundsFiredThisCycle = 0;
	for (int32 MuzzleIdx : MuzzlesToFire)
	{
		if ((bSimulatedFire ? Station.SimulatedAmmo : Station.CurrentAmmo) <= 0)
		{
			break;
		}

		const FGunMuzzle& Muzzle = Station.GunMuzzles[MuzzleIdx];

		FTransform MuzzleTransform = OwnerActor->GetActorTransform();
		if (MeshComp && Muzzle.MuzzleSocketName != NAME_None && MeshComp->DoesSocketExist(Muzzle.MuzzleSocketName))
		{
			MuzzleTransform = MeshComp->GetSocketTransform(Muzzle.MuzzleSocketName);
		}

		// Apply angular spread if configured
		FRotator FireRotation = MuzzleTransform.GetRotation().Rotator();
		if (Station.BulletSpreadDegrees > KINDA_SMALL_NUMBER)
		{
			const float HalfSpreadRad = FMath::DegreesToRadians(Station.BulletSpreadDegrees * 0.5f);
			const FVector SpreadDir = FMath::VRandCone(FireRotation.Vector(), HalfSpreadRad);
			FireRotation = SpreadDir.Rotation();
		}

		// In SIMULATE mode, do not register physical ballistic bullets
		if (!bSimulatedFire)
		{
			FActiveTracedBullet NewBullet;
			NewBullet.CurrentPosition = MuzzleTransform.GetLocation();
			NewBullet.Velocity = (FireRotation.Vector() * Muzzle.BulletSpeed) + AircraftVelocity;
			NewBullet.RemainingLifetime = Muzzle.BulletLifespan;
			NewBullet.GravityScale = Muzzle.GravityScale;
			NewBullet.DragCoefficient = Muzzle.DragCoefficient;
			NewBullet.TraceRadius = Muzzle.TraceRadius;
			NewBullet.TraceChannel = Muzzle.TraceChannel;
			NewBullet.StationIndex = StationIndex;
			NewBullet.MuzzleIndex = MuzzleIdx;

			ActiveTracedBullets.Add(NewBullet);
			SetComponentTickEnabled(true);

			if (bDebug)
			{
				FAircraftCombatDebug::DrawBulletTracer(World, MuzzleTransform.GetLocation(), MuzzleTransform.GetLocation() + (FireRotation.Vector() * 500.0f), 0.1f, FColor::Cyan);
			}
		}

		if (bSimulatedFire) Station.SimulatedAmmo = FMath::Max(0, Station.SimulatedAmmo - 1);
		else Station.CurrentAmmo = FMath::Max(0, Station.CurrentAmmo - 1);
		RoundsFiredThisCycle++;
	}

	const int32 RemainingAmmo = bSimulatedFire ? Station.SimulatedAmmo : Station.CurrentAmmo;
	const int32 Capacity = Station.GetMaxAmmo();
	const bool bEmpty = RemainingAmmo == 0;
	if (bEmpty && !bSimulatedFire) Station.Status = EStationStatus::Empty;
	if (RoundsFiredThisCycle > 0) OnStationAmmoChanged.Broadcast(StationIndex, RemainingAmmo, Capacity);
	if (bEmpty)
	{
		StopFiring(StationIndex);
		if (!bSimulatedFire)
		{
			OnStationStatusChanged.Broadcast(StationIndex, EStationStatus::Empty);
			OnStoresInventoryChanged.Broadcast();
		}
	}

	return RoundsFiredThisCycle > 0;
}

void UModularMissionManagement::SimulateTracedBullets(float DeltaTime)
{
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f) return;
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		ActiveTracedBullets.Empty();
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GunBulletSweptTrace), true, OwnerActor);
	QueryParams.AddIgnoredActor(OwnerActor);
	if (APawn* OwnerPawn = Cast<APawn>(OwnerActor))
	{
		QueryParams.AddIgnoredActor(OwnerPawn);
	}
	if (APawn* InstigatorPawn = OwnerActor->GetInstigator())
	{
		QueryParams.AddIgnoredActor(InstigatorPawn);
	}

	struct FPendingGunHit { int32 Station; int32 Muzzle; FHitResult Hit; };
	TArray<FPendingGunHit> PendingHits;
	for (int32 i = ActiveTracedBullets.Num() - 1; i >= 0; --i)
	{
		FActiveTracedBullet& Bullet = ActiveTracedBullets[i];
		if (Bullet.RemainingLifetime <= 0.0f)
		{
			ActiveTracedBullets.RemoveAtSwap(i);
			continue;
		}
		const float Step = FMath::Min(DeltaTime, Bullet.RemainingLifetime);
		Bullet.RemainingLifetime -= Step;
		FVector Travel = Bullet.Velocity * Step;

		// Apply aerodynamic drag deceleration: a = -v_hat * (v^2 * Cd)
		if (Bullet.DragCoefficient > 0.0f)
		{
			const float Speed = Bullet.Velocity.Size();
			const float DragStep = Speed * Bullet.DragCoefficient * Step;
			// Exact quadratic-drag step: finite hitches cannot reverse a bullet.
			if (DragStep > SMALL_NUMBER)
				Travel *= FMath::Loge(1.0f + DragStep) / DragStep;
			Bullet.Velocity /= 1.0f + DragStep;
		}

		// Apply gravity drop
		if (Bullet.GravityScale > 0.0f)
		{
			const FVector GravityAccel(0.0f, 0.0f, World->GetGravityZ() * Bullet.GravityScale);
			Travel += GravityAccel * (0.5f * Step * Step);
			Bullet.Velocity += GravityAccel * Step;
		}

		const FVector NextPosition = Bullet.CurrentPosition + Travel;

		FHitResult HitResult;
		bool bHit = false;

		if (Bullet.TraceRadius > KINDA_SMALL_NUMBER)
		{
			const FCollisionShape ColShape = FCollisionShape::MakeSphere(Bullet.TraceRadius);
			bHit = World->SweepSingleByChannel(
				HitResult,
				Bullet.CurrentPosition,
				NextPosition,
				FQuat::Identity,
				Bullet.TraceChannel,
				ColShape,
				QueryParams
			);
		}
		else
		{
			bHit = World->LineTraceSingleByChannel(
				HitResult,
				Bullet.CurrentPosition,
				NextPosition,
				Bullet.TraceChannel,
				QueryParams
			);
		}

		if (bHit)
		{
			if (bDebug)
			{
				FAircraftCombatDebug::DrawBulletImpact(World, HitResult.ImpactPoint, HitResult.ImpactNormal);
			}

			PendingHits.Add({Bullet.StationIndex, Bullet.MuzzleIndex, HitResult});
			ActiveTracedBullets.RemoveAtSwap(i);
		}
		else
		{
			if (bDebug)
			{
				FAircraftCombatDebug::DrawBulletTracer(World, Bullet.CurrentPosition, NextPosition, DeltaTime);
			}

			Bullet.CurrentPosition = NextPosition;
			if (Bullet.RemainingLifetime <= 0.0f) ActiveTracedBullets.RemoveAtSwap(i);
		}
	}
	// Dispatch after simulation: callbacks may fire again, reload, or destroy the
	// carrier, and must never invalidate an in-flight array reference.
	for (const FPendingGunHit& Pending : PendingHits)
	{
		if (!IsValid(OwnerActor) || OwnerActor->IsActorBeingDestroyed()) break;
		if (!World->IsGameWorld() || OwnerActor->HasAuthority())
			OnGunBulletHit.Broadcast(Pending.Station, Pending.Muzzle, Pending.Hit);
	}
}

AActor* UModularMissionManagement::ExecuteStoreRelease(FWeaponStation& Station, AActor* TargetActor, EWeaponLaunchFailureReason& OutFailReason)
{
	OutFailReason = EWeaponLaunchFailureReason::None;
	AActor* FiredActor = nullptr;
	FStationStore* ActiveStore = nullptr;
	bool bSpawnedForRelease = false;

	for (FStationStore& Store : Station.Stores)
	{
		if (Store.Status == EStoreStatus::Ready)
		{
			ActiveStore = &Store;
			FiredActor = Store.MountedActor;
			break;
		}
	}

	if (!ActiveStore)
	{
		OutFailReason = EWeaponLaunchFailureReason::AmmoDepleted;
		return nullptr;
	}

	// If mounted actor was missing but store has class, spawn on demand
	if (!IsValid(FiredActor) && ActiveStore->WeaponClass)
	{
		UWorld* World = GetWorld();
		AActor* OwnerActor = GetOwner();
		if (World && OwnerActor)
		{
			USceneComponent* MeshComp = ResolveAircraftMesh();
			FTransform SpawnTransform = OwnerActor->GetActorTransform();
			if (MeshComp && ActiveStore->SocketName != NAME_None && MeshComp->DoesSocketExist(ActiveStore->SocketName))
			{
				SpawnTransform = MeshComp->GetSocketTransform(ActiveStore->SocketName);
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = OwnerActor;
			SpawnParams.Instigator = Cast<APawn>(OwnerActor);
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			FiredActor = World->SpawnActor<AActor>(ActiveStore->WeaponClass, SpawnTransform, SpawnParams);
			bSpawnedForRelease = IsValid(FiredActor);
			if (bSpawnedForRelease)
			{
				if (MeshComp) FiredActor->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ActiveStore->SocketName);
				else FiredActor->AttachToActor(OwnerActor, FAttachmentTransformRules::KeepWorldTransform);
				if (AWeapon* Weapon = Cast<AWeapon>(FiredActor)) Weapon->SetMounted(true, OwnerActor);
			}
		}
	}

	if (!IsValid(FiredActor))
	{
		OutFailReason = EWeaponLaunchFailureReason::StoreSpawnFailed;
		return nullptr;
	}

	USceneComponent* MeshComp = ResolveAircraftMesh();
	auto RejectRelease = [&](EWeaponLaunchFailureReason Reason) -> AActor*
	{
		OutFailReason = Reason;
		if (bSpawnedForRelease && IsValid(FiredActor))
		{
			FiredActor->Destroy();
		}
		return nullptr;
	};

	TInlineComponentArray<UMasterWeaponComponent*> ReleaseComponents(FiredActor);
	if (ReleaseComponents.Num() > 1 || !HasRequiredWeaponComponent(Station.StoreType, ResolveStoreWeapon(FiredActor)))
		return RejectRelease(EWeaponLaunchFailureReason::NoWeaponConfigured);
	UMasterWeaponComponent* WeaponComp = ResolveStoreWeapon(FiredActor);
	if (Station.bUsesAmmoManagement && (!WeaponComp ||
		Cast<UMissileGuidanceComponent>(WeaponComp) || Cast<UDroppableItemComponent>(WeaponComp)))
		return RejectRelease(EWeaponLaunchFailureReason::InvalidStoreType);
	if (bSpawnedForRelease) TargetActor = GetCueTargetForWeapon(WeaponComp);

	if (WeaponComp)
	{
		WeaponComp->InitializeWeapon(Cast<APawn>(GetOwner()));
		ProgramWeaponIgnoreList(WeaponComp);

		if (UMissileGuidanceComponent* Guidance = Cast<UMissileGuidanceComponent>(WeaponComp))
		{
			FMissileLaunchConfiguration Launch;
			Launch.Carrier = Cast<APawn>(GetOwner());
			Launch.Target = Guidance->GetTargetSolution();
			Launch.bMadDog = !IsValid(TargetActor) && !IsValid(Guidance->GetLockedTarget());
			if (!IsValid(TargetActor) &&
				(Cast<URadarMissileGuidanceComponent>(Guidance) || Cast<UIRMissileGuidanceComponent>(Guidance)))
				Launch.Target = FMissileTargetSolution();

			if (IsValid(TargetActor) && !Cast<URadarMissileGuidanceComponent>(Guidance))
			{
				Launch.Target.bValid = true;
				Launch.Target.bMeasured = true;
				Launch.Target.TargetActor = TargetActor;
				Launch.Target.Position = TargetActor->GetActorLocation();
				Launch.Target.Velocity = TargetActor->GetVelocity();
				Launch.Target.MeasurementTimeSeconds = GetWorld()->GetTimeSeconds();
			}

			if (URadarMissileGuidanceComponent* RadarGuidance = Cast<URadarMissileGuidanceComponent>(Guidance))
			{
				if (IsValid(TargetActor))
				{
					Launch.Target.TargetActor = TargetActor;
				}

				if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
				{
					Launch.LaunchRadar = Radar;
					Launch.Uplink = Radar;
					Launch.Illuminator = Radar;

					FRadarTrack Track;
					Track.TrackID = INDEX_NONE;
					const bool bRemote = IsValid(TargetActor) && Radar->GetSelectedLinkedTrackForActor(TargetActor, Track);

					if (bRemote)
					{
						Launch.Illuminator = Radar->GetLinkedTrackSource(Track.TrackID);
						if (!IsValid(Launch.Illuminator))
						{
							return RejectRelease(EWeaponLaunchFailureReason::DatalinkSupportUnavailable);
						}
					}
					else if (IsValid(TargetActor) && !Radar->GetTrackByActor(TargetActor, Track))
					{
						Track.TrackID = INDEX_NONE;
					}

					if (!bRemote && Track.TrackID != INDEX_NONE &&
						(Track.Status == ERadarTrackStatus::Lost || Track.TrackAge > Radar->LocalCorrelationFreshnessSeconds))
					{
						Track.TrackID = INDEX_NONE;
					}

					if (IsValid(TargetActor) && Track.TrackID != INDEX_NONE)
					{
						Launch.LaunchTrackID = Track.TrackID;
						Launch.Target.bValid = true;
						Launch.Target.bMeasured = true;
						Launch.Target.Position = Track.LastKnownPosition;
						Launch.Target.Velocity = Track.EstimatedVelocity;
						Launch.Target.MeasurementTimeSeconds = GetWorld()->GetTimeSeconds() - Track.TrackAge;
						Launch.Target.TargetContactID = Track.ContactID;
						Launch.Target.SourceParticipantID = bRemote ? Track.SourceParticipantID : 0;
						Launch.Target.SourceTrackID = bRemote ? Track.SourceTrackID : Track.TrackID;
					}
					else if (IsValid(TargetActor))
					{
						Launch.Target.bValid = false;
					}
				}
			}
			else if (UARMMissileGuidanceComponent* ARM = Cast<UARMMissileGuidanceComponent>(Guidance))
			{
				if (!IsValid(TargetActor))
				{
					if (URadarWarningReceiverComponent* RWR = ResolveRWRComponent())
					{
						FRWRThreatEntry Threat;
						if (RWR->GetHighestThreat(Threat))
						{
							ARM->HandoffFromRWRThreat(Threat);
						}
					}
				}
			}

			if (!Guidance->PrepareLaunch(Launch))
			{
				return RejectRelease(EWeaponLaunchFailureReason::TargetOutOfEnvelope);
			}
		}

		WeaponComp->ActivateWeapon(true);

		EWeaponLaunchFailureReason WeaponCheckReason = EWeaponLaunchFailureReason::None;
		if (!WeaponComp->CanFireWeapon(WeaponCheckReason))
		{
			return RejectRelease(WeaponCheckReason);
		}
		if (IsWeaponReleaseInhibitedByIFF(WeaponComp, TargetActor))
		{
			return RejectRelease(EWeaponLaunchFailureReason::FriendlyTargetInhibit);
		}

		bool bWeaponFired = false;
		{
			TGuardValue<bool> MissionAuthorization(WeaponComp->bMissionReleaseAuthorized, true);
			bWeaponFired = WeaponComp->FireWeapon();
		}
		if (!bWeaponFired || !IsValid(FiredActor) || !IsValid(GetOwner()))
		{
			return RejectRelease(EWeaponLaunchFailureReason::ReleaseMechanismFailed);
		}
	}

	if (bSpawnedForRelease && Station.bUsesAmmoManagement) ActiveStore->MountedActor = FiredActor;

	// Physical Detachment & Kinematics
	if (!Station.bUsesAmmoManagement)
	{
		// Detach store from carrier aircraft
		FiredActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

		if (FiredActor->GetAttachParentActor() != nullptr)
		{
			return RejectRelease(EWeaponLaunchFailureReason::ReleaseMechanismFailed);
		}

		SetupStoreCollisionIgnores(FiredActor);

		if (AWeapon* Weapon = Cast<AWeapon>(FiredActor))
		{
			Weapon->SetMounted(false, GetOwner());
		}

		FTransform ReleaseTransform = GetOwner()->GetActorTransform();
		if (MeshComp && ActiveStore->SocketName != NAME_None && MeshComp->DoesSocketExist(ActiveStore->SocketName))
		{
			ReleaseTransform = MeshComp->GetSocketTransform(ActiveStore->SocketName);
		}
		const FVector WorldImpulse = ReleaseTransform.TransformVectorNoScale(Station.EjectionImpulse);

		if (WeaponComp)
		{
			WeaponComp->ApplyEjectionImpulse(WorldImpulse);
		}
		else
		{
			// Non-guided ordnance or ballistic bomb release
			UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(FiredActor->GetRootComponent());
			if (RootPrim)
			{
				RootPrim->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				RootPrim->SetSimulatePhysics(true);

				const APawn* OwnerPawn = Cast<APawn>(GetOwner());
				if (OwnerPawn)
				{
					if (const UPrimitiveComponent* OwnerPrim = Cast<UPrimitiveComponent>(OwnerPawn->GetRootComponent()))
					{
						FVector PtVel = OwnerPrim->IsSimulatingPhysics() ? OwnerPrim->GetPhysicsLinearVelocityAtPoint(FiredActor->GetActorLocation()) : OwnerPrim->GetPhysicsLinearVelocity();
						if (PtVel.IsNearlyZero())
						{
							PtVel = OwnerPawn->GetVelocity();
						}
						RootPrim->SetPhysicsLinearVelocity(PtVel);
					}
					else
					{
						RootPrim->SetPhysicsLinearVelocity(OwnerPawn->GetVelocity());
					}
				}

				RootPrim->AddImpulse(WorldImpulse, NAME_None, true);
			}
		}

		ActiveStore->MountedActor = nullptr;
		ActiveStore->Status = EStoreStatus::Fired;
	}
	else
	{
		// Persistent launcher / pod station (e.g. Gun Pod, Rocket Pod)
		// Store remains mounted to the aircraft hardpoint.
		SetupStoreCollisionIgnores(FiredActor);
	}

	return FiredActor;
}



int32 UModularMissionManagement::EmergencyJettisonAll()
{
	AActor* OwnerActor = GetOwner();
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && OwnerActor && !OwnerActor->HasAuthority())
	{
		ServerEmergencyJettisonAll();
		return 0;
	}

	int32 JettisonCount = 0;

	for (FWeaponStation& Station : Stations)
	{
		if (Station.bCanJettison && Station.StoreType != EStoreType::InternalCannon)
		{
			if (SelectiveJettisonStation(Station.StationIndex))
			{
				JettisonCount++;
			}
		}
	}

	return JettisonCount;
}

bool UModularMissionManagement::SelectiveJettisonStation(int32 StationIndex)
{
	if (bStoreReleaseInProgress) return false;
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}

	FWeaponStation& Station = Stations[ArrayIndex];
	if (Station.StoreType == EStoreType::InternalCannon || !Station.bCanJettison)
	{
		return false;
	}

	AActor* OwnerActor = GetOwner();
	if (UWorld* World = GetWorld(); World && World->IsGameWorld() && OwnerActor && !OwnerActor->HasAuthority())
	{
		ServerSelectiveJettisonStation(StationIndex);
		return true;
	}

	USceneComponent* MeshComp = ResolveAircraftMesh();
	bool bJettisonedAny = false;
	if (SelectedStationIndex == StationIndex) UnbindActiveWeaponDelegates();

	for (FStationStore& Store : Station.Stores)
	{
		if (Store.Status == EStoreStatus::Ready && IsValid(Store.MountedActor))
		{
			AActor* StoreActor = Store.MountedActor;
			StoreActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

			SetupStoreCollisionIgnores(StoreActor);

			if (AWeapon* Weapon = Cast<AWeapon>(StoreActor))
			{
				Weapon->SetMounted(false, GetOwner());
			}

			FTransform JettisonTransform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
			if (MeshComp && Store.SocketName != NAME_None && MeshComp->DoesSocketExist(Store.SocketName))
			{
				JettisonTransform = MeshComp->GetSocketTransform(Store.SocketName);
			}
			const FVector WorldImpulse = JettisonTransform.TransformVectorNoScale(Station.EjectionImpulse);

			if (UMasterWeaponComponent* WeaponComp = ResolveStoreWeapon(StoreActor))
			{
				WeaponComp->InitializeWeapon(Cast<APawn>(GetOwner()));
				if (UMissileGuidanceComponent* Missile = Cast<UMissileGuidanceComponent>(WeaponComp))
				{
					Missile->JettisonInert(WorldImpulse);
				}
				else if (UDroppableItemComponent* Drop = Cast<UDroppableItemComponent>(WeaponComp))
				{
					Drop->ApplyEjectionImpulse(WorldImpulse);
					Drop->DetachWeapon();
				}
				else
				{
					// Empty persistent pods still need inert ballistic movement.
					WeaponComp->ActivateWeapon(false);
					WeaponComp->DetachWeapon();
					WeaponComp->SetUpdatedComponent(StoreActor->GetRootComponent());
					WeaponComp->Velocity = (GetOwner() ? GetOwner()->GetVelocity() : FVector::ZeroVector) + WorldImpulse;
					WeaponComp->ProjectileGravityScale = 1.0f;
					WeaponComp->MaxSpeed = 0.0f;
					WeaponComp->SetActive(true);
					WeaponComp->SetComponentTickEnabled(true);
					WeaponComp->UpdateComponentVelocity();
				}
			}
			else if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(StoreActor->GetRootComponent()))
			{
				RootPrim->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				RootPrim->SetSimulatePhysics(true);
				if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
				{
					RootPrim->SetPhysicsLinearVelocity(OwnerPawn->GetVelocity());
				}
				RootPrim->AddImpulse(WorldImpulse, NAME_None, true);
			}

			// Set clean lifespan for jettisoned store debris
			StoreActor->SetLifeSpan(20.0f);
			
			Store.MountedActor = nullptr;
			Store.Status = EStoreStatus::Fired;
			bJettisonedAny = true;
		}
	}

	if (!bJettisonedAny)
	{
		BindActiveWeaponDelegates();
		return false;
	}

	Station.CurrentAmmo = 0;
	Station.SimulatedAmmo = 0;
	Station.Status = EStationStatus::Jettisoned;

	OnStationStatusChanged.Broadcast(StationIndex, EStationStatus::Jettisoned);
	OnStationAmmoChanged.Broadcast(StationIndex, 0, Station.GetMaxAmmo());
	OnWeaponJettisoned.Broadcast(StationIndex, Station.StoreType);
	OnStoresInventoryChanged.Broadcast();
	BindActiveWeaponDelegates();

	return true;
}

int32 UModularMissionManagement::JettisonByType(EStoreType InStoreType)
{
	int32 JettisonCount = 0;

	for (FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == InStoreType && Station.bCanJettison)
		{
			if (SelectiveJettisonStation(Station.StationIndex))
			{
				JettisonCount++;
			}
		}
	}

	return JettisonCount;
}

bool UModularMissionManagement::ConfigureStation(int32 StationIndex, const FWeaponStation& NewStationConfig)
{
	if (bStoreReleaseInProgress || !HasInventoryAuthority(this)) return false;
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}
	StopFiring(StationIndex);
	GunMuzzleIndices.Remove(StationIndex);
	if (SelectedStationIndex == StationIndex) UnbindActiveWeaponDelegates();

	for (UStaticMeshComponent* Pylon : Stations[ArrayIndex].SpawnedPylons)
		if (IsValid(Pylon)) Pylon->DestroyComponent();
	for (FStationStore& Store : Stations[ArrayIndex].Stores)
	{
		if (IsValid(Store.MountedActor))
		{
			Store.MountedActor->Destroy();
			Store.MountedActor = nullptr;
		}
	}

	Stations[ArrayIndex] = NewStationConfig;
	Stations[ArrayIndex].StationIndex = StationIndex;
	Stations[ArrayIndex].SpawnedPylons.Reset();
	for (FStationStore& Store : Stations[ArrayIndex].Stores)
	{
		Store.MountedActor = nullptr;
		Store.Status = EStoreStatus::Ready;
	}
	Stations[ArrayIndex].CurrentAmmo = FMath::Clamp(NewStationConfig.CurrentAmmo, 0, FMath::Max(0, NewStationConfig.MaxAmmo));
	Stations[ArrayIndex].SimulatedAmmo = Stations[ArrayIndex].CurrentAmmo;
	Stations[ArrayIndex].Status = EStationStatus::Ready;
	SpawnAllPylons();
	SpawnStoreForStation(StationIndex);
	ProgramAllWeaponsIgnoreLists();
	if (SelectedStationIndex == StationIndex) PrepareActiveWeaponOnStation(StationIndex);
	BindActiveWeaponDelegates();
	OnStoresInventoryChanged.Broadcast();
	return true;
}

bool UModularMissionManagement::ReloadStation(int32 StationIndex, int32 AmmoCount)
{
	if (bStoreReleaseInProgress || !HasInventoryAuthority(this)) return false;
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}
	StopFiring(StationIndex);
	GunMuzzleIndices.Remove(StationIndex);
	if (SelectedStationIndex == StationIndex) UnbindActiveWeaponDelegates();

	FWeaponStation& Station = Stations[ArrayIndex];
	
	if (Station.StoreType == EStoreType::InternalCannon || Station.bUsesAmmoManagement)
	{
		Station.CurrentAmmo = FMath::Clamp(AmmoCount, 0, FMath::Max(0, Station.MaxAmmo));
		if (Station.StoreType == EStoreType::InternalCannon) Station.SimulatedAmmo = Station.CurrentAmmo;
		for (FStationStore& Store : Station.Stores) Store.Status = EStoreStatus::Ready;
	}
	else
	{
		for (int32 i = 0; i < Station.Stores.Num(); i++)
		{
			if (i < AmmoCount)
			{
				Station.Stores[i].Status = EStoreStatus::Ready;
			}
			else
			{
				if (IsValid(Station.Stores[i].MountedActor)) Station.Stores[i].MountedActor->Destroy();
				Station.Stores[i].MountedActor = nullptr;
				Station.Stores[i].Status = EStoreStatus::Fired;
			}
		}
	}

	Station.Status = (Station.GetCurrentAmmo() > 0) ? ((Station.StationIndex == SelectedStationIndex) ? EStationStatus::Selected : EStationStatus::Ready) : EStationStatus::Empty;
	SpawnStoreForStation(StationIndex);
	ProgramAllWeaponsIgnoreLists();
	if (SelectedStationIndex == StationIndex) PrepareActiveWeaponOnStation(StationIndex);
	BindActiveWeaponDelegates();

	OnStationAmmoChanged.Broadcast(StationIndex, Station.GetCurrentAmmo(), Station.GetMaxAmmo());
	OnStationStatusChanged.Broadcast(StationIndex, Station.Status);
	OnStoresInventoryChanged.Broadcast();
	return true;
}

void UModularMissionManagement::ReloadAllStations()
{
	if (bStoreReleaseInProgress || !HasInventoryAuthority(this)) return;
	TArray<int32> StationIDs;
	for (const FWeaponStation& Station : Stations) StationIDs.Add(Station.StationIndex);
	for (int32 ID : StationIDs)
	{
		FWeaponStation Station;
		if (GetStation(ID, Station)) ReloadStation(ID, Station.GetMaxAmmo());
	}
}

void UModularMissionManagement::ServerSetMasterArmMode_Implementation(EMasterArmMode InMode)
{
	SetMasterArmMode(InMode);
}

void UModularMissionManagement::ServerSetMasterMode_Implementation(EAircraftMasterMode InMode)
{
	SetMasterMode(InMode);
}

void UModularMissionManagement::ServerSelectStation_Implementation(int32 StationIndex)
{
	SelectStation(StationIndex);
}

void UModularMissionManagement::ServerSelectNextStation_Implementation()
{
	SelectNextStation();
}

void UModularMissionManagement::ServerSelectPreviousStation_Implementation()
{
	SelectPreviousStation();
}

void UModularMissionManagement::ServerSelectNextStationOfStoreType_Implementation(EStoreType InStoreType)
{
	SelectNextStationOfStoreType(InStoreType);
}

void UModularMissionManagement::ServerSlaveSelectedWeaponToDirection_Implementation(FVector InWorldDirection)
{
	SlaveSelectedWeaponToDirection(InWorldDirection);
}

void UModularMissionManagement::ServerSlaveSelectedWeaponToLocation_Implementation(FVector InWorldLocation)
{
	SlaveSelectedWeaponToLocation(InWorldLocation);
}

void UModularMissionManagement::ServerSlaveSelectedWeaponToBoresight_Implementation()
{
	SlaveSelectedWeaponToBoresight();
}

void UModularMissionManagement::ServerSetSelectedWeaponCaged_Implementation(bool bCaged)
{
	SetSelectedWeaponCaged(bCaged);
}

void UModularMissionManagement::ServerLockActor_Implementation(AActor* InTarget)
{
	if (!IsValid(InTarget))
	{
		ClearLockedActor();
		return;
	}
	UAircraftRadarComponent* Radar = ResolveRadarComponent();
	if (!Radar) return;
	if (Radar->IsSTTLocked() && Radar->GetSTTLockedActor() == InTarget)
	{
		LockActor(InTarget);
		return;
	}
	FRadarTrack Linked;
	if (Radar->GetSelectedLinkedTrackForActor(InTarget, Linked))
	{
		UAircraftRadarComponent* Source = Radar->GetLinkedTrackSource(Linked.TrackID);
		if (IsValid(Source) && Source->IsContinuousWaveIlluminating(InTarget))
			LockActor(InTarget);
	}
}

void UModularMissionManagement::ServerBugActor_Implementation(AActor* InTarget)
{
	if (!IsValid(InTarget))
	{
		ClearBuggedActor();
		return;
	}
	if (InTarget == GetOwner()) return;
	if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
	{
		FRadarTrack Bug;
		FRadarTrack Linked;
		const bool bLocalBug = Radar->GetBuggedTrackID() >= 0 &&
			Radar->GetTrackByID(Radar->GetBuggedTrackID(), Bug) && Bug.TrackedActor.Get() == InTarget;
		if (!bLocalBug && !Radar->GetSelectedLinkedTrackForActor(InTarget, Linked)) return;
	}
	BugActor(InTarget);
}

void UModularMissionManagement::ServerFire_Implementation(int32 StationIndex)
{
	EWeaponLaunchFailureReason PreCheckReason = EWeaponLaunchFailureReason::None;
	if (!CanFire(StationIndex, PreCheckReason))
	{
		LastLaunchFailureReason = PreCheckReason;
		ClientFireResult(StationIndex, false, PreCheckReason);
		return;
	}

	const bool bSucceeded = Fire(StationIndex);
	const EWeaponLaunchFailureReason FinalReason = bSucceeded ? EWeaponLaunchFailureReason::None : LastLaunchFailureReason;
	ClientFireResult(StationIndex, bSucceeded, FinalReason);
}

void UModularMissionManagement::ClientFireResult_Implementation(int32 StationIndex,
	bool bSucceeded, EWeaponLaunchFailureReason Reason)
{
	OnWeaponLaunchResult.Broadcast(StationIndex, bSucceeded, Reason);
}

void UModularMissionManagement::ServerStartFiring_Implementation(int32 StationIndex)
{
	StartFiring(StationIndex);
}

void UModularMissionManagement::ServerStopFiring_Implementation(int32 StationIndex)
{
	StopFiring(StationIndex);
}

void UModularMissionManagement::ServerEmergencyJettisonAll_Implementation()
{
	EmergencyJettisonAll();
}

void UModularMissionManagement::ServerSelectiveJettisonStation_Implementation(int32 StationIndex)
{
	SelectiveJettisonStation(StationIndex);
}

void UModularMissionManagement::ServerJettisonByType_Implementation(EStoreType InStoreType)
{
	JettisonByType(InStoreType);
}
