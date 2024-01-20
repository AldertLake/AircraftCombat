// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "ModularMissionManagement.h"
#include "Weapon.h"
#include "MissileGuidanceComponent.h"
#include "RadarMissileGuidanceComponent.h"
#include "IRMissileGuidanceComponent.h"
#include "AircraftRadarComponent.h"
#include "MasterWeaponComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "AircraftCombatDebug.h"

UModularMissionManagement::UModularMissionManagement()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	MasterArmMode = EMasterArmMode::Safe;
	MasterMode = EAircraftMasterMode::AirToAir;
	bAutoSpawnStoresOnBeginPlay = true;
	bAutoStepStationOnFire = true;
	SelectedStationIndex = 1;
}

void UModularMissionManagement::BeginPlay()
{
	Super::BeginPlay();

	// Auto-discover aircraft radar if present
	ResolveRadarComponent();

	if (bAutoSpawnStoresOnBeginPlay)
	{
		InitializeStores();
	}

	// Select first available station with ammunition if available
	if (Stations.Num() > 0)
	{
		bool bFoundSelected = false;
		for (const FWeaponStation& Station : Stations)
		{
			if (Station.GetCurrentAmmo() > 0 && Station.StoreType != EStoreType::None)
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

	if (IsValid(DesignatedTarget))
	{
		DesignatedTarget->OnDestroyed.RemoveDynamic(this, &UModularMissionManagement::HandleDesignatedTargetDestroyed);
	}
	DesignatedTarget = nullptr;

	if (CachedRadarComponent)
	{
		CachedRadarComponent->OnRadarTrackSelected.RemoveDynamic(this, &UModularMissionManagement::HandleRadarTrackSelected);
		CachedRadarComponent->OnRadarTrackDeselected.RemoveDynamic(this, &UModularMissionManagement::HandleRadarTrackDeselected);
		CachedRadarComponent->OnRadarLockAcquired.RemoveDynamic(this, &UModularMissionManagement::HandleRadarLockAcquired);
		CachedRadarComponent->OnRadarLockLost.RemoveDynamic(this, &UModularMissionManagement::HandleRadarLockLost);
		CachedRadarComponent->OnRadarContactLost.RemoveDynamic(this, &UModularMissionManagement::HandleRadarContactLost);
		CachedRadarComponent->OnRadarAllContactsCleared.RemoveDynamic(this, &UModularMissionManagement::HandleRadarAllContactsCleared);
		CachedRadarComponent = nullptr;
	}

	DestroyAllPylons();
	Super::EndPlay(EndPlayReason);
}

USceneComponent* UModularMissionManagement::ResolveAircraftMesh() const
{
	if (CachedAircraftMesh)
	{
		return CachedAircraftMesh;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	// Look for a PrimitiveComponent / Mesh on the owning actor
	TArray<USceneComponent*> SceneComponents;
	OwnerActor->GetComponents<USceneComponent>(SceneComponents);

	for (USceneComponent* Comp : SceneComponents)
	{
		if (Comp && (Comp->ComponentHasTag(FName(TEXT("AircraftMesh"))) || Comp->GetName().Contains(TEXT("Mesh"))))
		{
			CachedAircraftMesh = Comp;
			return CachedAircraftMesh;
		}
	}

	// Fallback to Owner Root Component
	CachedAircraftMesh = OwnerActor->GetRootComponent();
	return CachedAircraftMesh;
}

void UModularMissionManagement::InitializeStores(USceneComponent* InAircraftMesh)
{
	if (InAircraftMesh)
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
	if (!World || !OwnerActor)
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
		if (UMasterWeaponComponent* WeaponComp = SpawnedActor->FindComponentByClass<UMasterWeaponComponent>())
		{
			WeaponComp->InitializeWeapon(Cast<APawn>(OwnerActor));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("SpawnStoreForStation: Spawned weapon actor %s is missing a UMasterWeaponComponent!"), *SpawnedActor->GetName());
		}
	}

	Station.Status = (Station.StationIndex == SelectedStationIndex) ? EStationStatus::Selected : EStationStatus::Ready;
}

void UModularMissionManagement::SpawnAllPylons()
{
	AActor* OwnerActor = GetOwner();
	USceneComponent* MeshComp = ResolveAircraftMesh();

	if (!OwnerActor || !MeshComp)
	{
		return;
	}

	for (FWeaponStation& Station : Stations)
	{
		// Clean existing
		for (UStaticMeshComponent* PylonComp : Station.SpawnedPylons)
		{
			if (IsValid(PylonComp))
			{
				PylonComp->DestroyComponent();
			}
		}
		Station.SpawnedPylons.Empty();

		for (int32 PylonIdx = 0; PylonIdx < Station.Pylons.Num(); ++PylonIdx)
		{
			const FStationPylon& PylonData = Station.Pylons[PylonIdx];
			if (PylonData.PylonMesh)
			{
				FString CompName = FString::Printf(TEXT("Pylon_Sta%d_%d"), Station.StationIndex, PylonIdx);
				UStaticMeshComponent* PylonComp = NewObject<UStaticMeshComponent>(OwnerActor, FName(*CompName));
				if (PylonComp)
				{
					PylonComp->SetStaticMesh(PylonData.PylonMesh);
					PylonComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					PylonComp->RegisterComponent();
					PylonComp->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, PylonData.SocketName);
					
					Station.SpawnedPylons.Add(PylonComp);
				}
			}
		}
	}
}

void UModularMissionManagement::DestroyAllPylons()
{
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
	for (FWeaponStation& Station : Stations)
	{
		SpawnStoreForStation(Station.StationIndex);
	}
	ProgramAllWeaponsIgnoreLists();
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
				if (UMasterWeaponComponent* Comp = Store.MountedActor->FindComponentByClass<UMasterWeaponComponent>())
				{
					ProgramWeaponIgnoreList(Comp);
				}
			}
		}
	}
}

void UModularMissionManagement::DestroyMountedStores()
{
	for (FWeaponStation& Station : Stations)
	{
		for (FStationStore& Store : Station.Stores)
		{
			if (IsValid(Store.MountedActor))
			{
				Store.MountedActor->Destroy();
				Store.MountedActor = nullptr;
			}
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
	return false;
}

bool UModularMissionManagement::GetStationBySocket(FName SocketName, FWeaponStation& OutStation) const
{
	for (const FWeaponStation& Station : Stations)
	{
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
		}
	}

	SelectedStationIndex = StationIndex;
	if (Stations[TargetIdx].Status != EStationStatus::Fault && Stations[TargetIdx].Status != EStationStatus::Jettisoned)
	{
		Stations[TargetIdx].Status = EStationStatus::Selected;
	}

	OnStationSelected.Broadcast(SelectedStationIndex, Stations[TargetIdx]);

	// Activate the new weapon if we are in a combat mode
	if (MasterMode != EAircraftMasterMode::Navigation)
	{
		if (UMasterWeaponComponent* NewWeapon = GetActiveWeaponComponent(SelectedStationIndex))
		{
			ProgramWeaponIgnoreList(NewWeapon);
			NewWeapon->ActivateWeapon(true);

			// If we already have a designated target, cue or slave the newly selected weapon to it
			if (IsValid(DesignatedTarget))
			{
				if (UIRMissileGuidanceComponent* IRGuidance = Cast<UIRMissileGuidanceComponent>(NewWeapon))
				{
					IRGuidance->SlaveToDesignatedTarget(DesignatedTarget);
				}
				else if (URadarMissileGuidanceComponent* RadarGuidance = Cast<URadarMissileGuidanceComponent>(NewWeapon))
				{
					if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
					{
						RadarGuidance->SetParentRadar(Radar);
						FRadarTrack TrackInfo;
						if (Radar->GetTrackByActor(DesignatedTarget, TrackInfo))
						{
							RadarGuidance->SetInertialTarget(TrackInfo.LastKnownPosition, TrackInfo.EstimatedVelocity);
						}
						else
						{
							RadarGuidance->SetInertialTarget(DesignatedTarget->GetActorLocation(), DesignatedTarget->GetVelocity());
						}
					}
					else
					{
						RadarGuidance->SetInertialTarget(DesignatedTarget->GetActorLocation(), DesignatedTarget->GetVelocity());
					}
				}
			}
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
	const int32 StartIdx = (CurrentIdx != INDEX_NONE) ? CurrentIdx : 0;

	for (int32 i = 1; i <= Stations.Num(); ++i)
	{
		const int32 TestIdx = (StartIdx + i) % Stations.Num();
		if (Stations[TestIdx].GetCurrentAmmo() > 0 && Stations[TestIdx].StoreType != EStoreType::None)
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
	const int32 StartIdx = (CurrentIdx != INDEX_NONE) ? CurrentIdx : 0;

	for (int32 i = 1; i <= Stations.Num(); ++i)
	{
		int32 TestIdx = (StartIdx - i) % Stations.Num();
		if (TestIdx < 0)
		{
			TestIdx += Stations.Num();
		}

		if (Stations[TestIdx].GetCurrentAmmo() > 0 && Stations[TestIdx].StoreType != EStoreType::None)
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
	const int32 StartIdx = (CurrentIdx != INDEX_NONE) ? CurrentIdx : 0;

	for (int32 i = 1; i <= Stations.Num(); ++i)
	{
		const int32 TestIdx = (StartIdx + i) % Stations.Num();
		if (Stations[TestIdx].StoreType == InStoreType && Stations[TestIdx].GetCurrentAmmo() > 0)
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
		if (Station.GetCurrentAmmo() > 0 && Station.StoreType != EStoreType::None)
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
		if (Station.StoreType == InStoreType && Station.GetCurrentAmmo() > 0)
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

		// Fallback: If station uses ammo management and ammo remains, return the primary store definition
		if (Station.bUsesAmmoManagement && Station.GetCurrentAmmo() > 0 && Station.Stores.Num() > 0)
		{
			OutStore = Station.Stores[0];
			return true;
		}
	}

	OutStore = FStationStore();
	return false;
}

EStoreType UModularMissionManagement::GetSelectedStoreType() const
{
	FWeaponStation Station;
	if (GetSelectedStation(Station))
	{
		return Station.StoreType;
	}
	return EStoreType::None;
}

AActor* UModularMissionManagement::GetActiveWeaponActor(int32 StationIndex) const
{
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex != INDEX_NONE)
	{
		for (const FStationStore& Store : Stations[ArrayIndex].Stores)
		{
			if (Store.Status == EStoreStatus::Ready && IsValid(Store.MountedActor))
			{
				return Store.MountedActor;
			}
		}
	}
	return nullptr;
}

UMasterWeaponComponent* UModularMissionManagement::GetActiveWeaponComponent(int32 StationIndex) const
{
	if (AActor* WeaponActor = GetActiveWeaponActor(StationIndex))
	{
		return WeaponActor->FindComponentByClass<UMasterWeaponComponent>();
	}
	return nullptr;
}

void UModularMissionManagement::SetMasterArmMode(EMasterArmMode InMode)
{
	if (MasterArmMode != InMode)
	{
		MasterArmMode = InMode;
		if (MasterArmMode == EMasterArmMode::Safe)
		{
			StopFiring(SelectedStationIndex);
		}
		OnMasterArmChanged.Broadcast(MasterArmMode);
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

		// Coupling: Master combat mode automatically configures the radar operating mode
		if (bSyncRadarWithMasterMode)
		{
			if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
			{
				switch (MasterMode)
				{
					case EAircraftMasterMode::Navigation:
						Radar->SetRadarMode(ERadarOperatingMode::Standby);
						break;
					case EAircraftMasterMode::AirToAir:
						Radar->SetRadarMode(ERadarOperatingMode::Search);
						break;
					case EAircraftMasterMode::AirToGround:
						Radar->SetRadarMode(ERadarOperatingMode::GroundMapping);
						break;
					case EAircraftMasterMode::Dogfight:
						Radar->SetRadarMode(ERadarOperatingMode::AirCombatManeuver);
						Radar->SetACMSubMode(ERadarACMSubMode::Boresight);
						SelectDogfightStation();
						break;
					case EAircraftMasterMode::MissileOverride:
						Radar->SetRadarMode(ERadarOperatingMode::TrackWhileScan);
						SelectBVRStation();
						break;
					default:
						break;
				}
			}
		}

		OnMasterModeChanged.Broadcast(MasterMode);
	}
}

void UModularMissionManagement::SetDesignatedTarget(AActor* InTarget)
{
	// Validate new target (ensure it exists and is not pending destruction)
	AActor* ValidNewTarget = IsValid(InTarget) ? InTarget : nullptr;

	// If we are already tracking this exact valid target, no update needed
	if (DesignatedTarget == ValidNewTarget && ValidNewTarget != nullptr)
	{
		return;
	}

	// Safely unbind from previously designated target if and only if it is still valid
	if (IsValid(DesignatedTarget))
	{
		DesignatedTarget->OnDestroyed.RemoveDynamic(this, &UModularMissionManagement::HandleDesignatedTargetDestroyed);
	}

	DesignatedTarget = ValidNewTarget;

	if (DesignatedTarget)
	{
		DesignatedTarget->OnDestroyed.AddUniqueDynamic(this, &UModularMissionManagement::HandleDesignatedTargetDestroyed);
		OnTargetDesignated.Broadcast(DesignatedTarget);
	}
	else
	{
		OnTargetCleared.Broadcast();
	}

	// Update corresponding radar track ID if radar is tracking this actor
	if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
	{
		if (DesignatedTarget)
		{
			FRadarTrack ExistingTrack;
			if (Radar->GetTrackByActor(DesignatedTarget, ExistingTrack))
			{
				DesignatedRadarTrackID = ExistingTrack.TrackID;
			}
			else
			{
				DesignatedRadarTrackID = INDEX_NONE;
			}
		}
		else
		{
			DesignatedRadarTrackID = INDEX_NONE;
		}

		// Integrate with aircraft radar system if present (preventing recursive ping-pong loops)
		if (!bIsSyncingTarget)
		{
			TGuardValue<bool> SyncGuard(bIsSyncingTarget, true);
			if (DesignatedTarget)
			{
				if (Radar->GetRadarMode() == ERadarOperatingMode::TrackWhileScan)
				{
					// In TWS mode, bug the track (PDT) rather than forcing STT lock which alerts enemy RWR
					if (DesignatedRadarTrackID != INDEX_NONE)
					{
						Radar->CommandBugTrack(DesignatedRadarTrackID);
					}
					else
					{
						DesignatedRadarTrackID = Radar->AcquireOrLockActor(DesignatedTarget);
					}
				}
				else
				{
					DesignatedRadarTrackID = Radar->AcquireOrLockActor(DesignatedTarget);
				}
			}
			else
			{
				if (Radar->IsSTTLocked())
				{
					Radar->BreakLock();
				}
				else if (Radar->GetRadarMode() == ERadarOperatingMode::TrackWhileScan)
				{
					FRadarTrack BuggedTrack;
					if (Radar->GetSelectedTrack(BuggedTrack))
					{
						Radar->CommandBugTrack(BuggedTrack.TrackID);
					}
				}
			}
		}
	}

	// Forward target to missile mounted on currently selected station if present
	const int32 ArrayIndex = FindStationArrayIndex(SelectedStationIndex);
	if (ArrayIndex != INDEX_NONE)
	{
		for (const FStationStore& Store : Stations[ArrayIndex].Stores)
		{
			if (Store.Status == EStoreStatus::Ready && IsValid(Store.MountedActor))
			{
				if (UMissileGuidanceComponent* Guidance = Store.MountedActor->FindComponentByClass<UMissileGuidanceComponent>())
				{
					if (UIRMissileGuidanceComponent* IRGuidance = Cast<UIRMissileGuidanceComponent>(Guidance))
					{
						if (DesignatedTarget)
						{
							IRGuidance->SlaveToDesignatedTarget(DesignatedTarget);
						}
						else
						{
							IRGuidance->LockMissile(nullptr);
						}
					}
					else
					{
						Guidance->LockMissile(DesignatedTarget);
					}

					// If this is a radar missile, cue with initial radar track data
					if (URadarMissileGuidanceComponent* RadarGuidance = Cast<URadarMissileGuidanceComponent>(Guidance))
					{
						if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
						{
							RadarGuidance->SetParentRadar(Radar);
							if (DesignatedTarget)
							{
								FRadarTrack TrackInfo;
								if (Radar->GetTrackByActor(DesignatedTarget, TrackInfo))
								{
									RadarGuidance->SetInertialTarget(TrackInfo.LastKnownPosition, TrackInfo.EstimatedVelocity);
								}
								else
								{
									RadarGuidance->SetInertialTarget(DesignatedTarget->GetActorLocation(), DesignatedTarget->GetVelocity());
								}
							}
						}
					}
				}
				break;
			}
		}
	}
}

void UModularMissionManagement::ClearDesignatedTarget()
{
	SetDesignatedTarget(nullptr);
}

UAircraftRadarComponent* UModularMissionManagement::ResolveRadarComponent()
{
	if (!CachedRadarComponent)
	{
		if (AActor* OwnerActor = GetOwner())
		{
			CachedRadarComponent = OwnerActor->FindComponentByClass<UAircraftRadarComponent>();
			if (CachedRadarComponent)
			{
				BindRadarDelegates(CachedRadarComponent);
			}
		}
	}
	return CachedRadarComponent;
}

UAircraftRadarComponent* UModularMissionManagement::GetRadarComponent() const
{
	if (CachedRadarComponent)
	{
		return CachedRadarComponent;
	}

	if (AActor* OwnerActor = GetOwner())
	{
		return OwnerActor->FindComponentByClass<UAircraftRadarComponent>();
	}

	return nullptr;
}

void UModularMissionManagement::BindRadarDelegates(UAircraftRadarComponent* Radar)
{
	if (!Radar)
	{
		return;
	}

	Radar->OnRadarTrackSelected.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarTrackSelected);
	Radar->OnRadarTrackDeselected.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarTrackDeselected);
	Radar->OnRadarLockAcquired.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarLockAcquired);
	Radar->OnRadarLockLost.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarLockLost);
	Radar->OnRadarContactLost.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarContactLost);
	Radar->OnRadarAllContactsCleared.AddUniqueDynamic(this, &UModularMissionManagement::HandleRadarAllContactsCleared);
}

void UModularMissionManagement::HandleRadarTrackSelected(const FRadarTrack& SelectedTrack)
{
	if (bIsSyncingTarget)
	{
		return;
	}

	if (SelectedTrack.TrackedActor.IsValid())
	{
		TGuardValue<bool> SyncGuard(bIsSyncingTarget, true);
		DesignatedRadarTrackID = SelectedTrack.TrackID;
		SetDesignatedTarget(SelectedTrack.TrackedActor.Get());
	}
}

void UModularMissionManagement::HandleRadarTrackDeselected(int32 TrackID)
{
	if (bIsSyncingTarget)
	{
		return;
	}

	// Only clear if the deselected track corresponds to our designated target
	if (DesignatedTarget && DesignatedRadarTrackID != INDEX_NONE && DesignatedRadarTrackID == TrackID)
	{
		if (UAircraftRadarComponent* Radar = GetRadarComponent())
		{
			// Check if we still have an STT lock on this target
			if (Radar->IsSTTLocked() && Radar->GetSTTLockedActor() == DesignatedTarget)
			{
				return;
			}
		}

		TGuardValue<bool> SyncGuard(bIsSyncingTarget, true);
		DesignatedRadarTrackID = INDEX_NONE;
		SetDesignatedTarget(nullptr);
	}
}

void UModularMissionManagement::HandleRadarLockAcquired(const FRadarTrack& LockedTrack)
{
	if (bIsSyncingTarget)
	{
		return;
	}

	if (LockedTrack.TrackedActor.IsValid())
	{
		TGuardValue<bool> SyncGuard(bIsSyncingTarget, true);
		DesignatedRadarTrackID = LockedTrack.TrackID;
		SetDesignatedTarget(LockedTrack.TrackedActor.Get());
	}
}

void UModularMissionManagement::HandleRadarLockLost(int32 TrackID)
{
	if (bIsSyncingTarget)
	{
		return;
	}

	// Only clear if the lost lock was our designated target!
	if (DesignatedTarget && DesignatedRadarTrackID != INDEX_NONE && DesignatedRadarTrackID == TrackID)
	{
		if (UAircraftRadarComponent* Radar = GetRadarComponent())
		{
			// Check if target is still retained as a bugged track in TWS
			FRadarTrack Track;
			if (Radar->GetTrackByID(TrackID, Track) && Track.bIsBugged)
			{
				return;
			}
		}

		TGuardValue<bool> SyncGuard(bIsSyncingTarget, true);
		DesignatedRadarTrackID = INDEX_NONE;
		SetDesignatedTarget(nullptr);
	}
}

void UModularMissionManagement::HandleRadarContactLost(int32 TrackID)
{
	if (bIsSyncingTarget)
	{
		return;
	}

	// Only clear if the lost contact was our designated target!
	if (DesignatedTarget && DesignatedRadarTrackID != INDEX_NONE && DesignatedRadarTrackID == TrackID)
	{
		TGuardValue<bool> SyncGuard(bIsSyncingTarget, true);
		DesignatedRadarTrackID = INDEX_NONE;
		SetDesignatedTarget(nullptr);
	}
}

void UModularMissionManagement::HandleRadarAllContactsCleared()
{
	if (bIsSyncingTarget)
	{
		return;
	}

	// Only clear if our designated target came from radar
	if (DesignatedTarget && DesignatedRadarTrackID != INDEX_NONE)
	{
		TGuardValue<bool> SyncGuard(bIsSyncingTarget, true);
		DesignatedRadarTrackID = INDEX_NONE;
		SetDesignatedTarget(nullptr);
	}
}

bool UModularMissionManagement::CalculateMissileLaunchZone(int32 StationIndex, float& OutRmin, float& OutRne, float& OutRmax, bool& OutInShootingEnvelope) const
{
	OutRmin = 0.0f;
	OutRne = 0.0f;
	OutRmax = 0.0f;
	OutInShootingEnvelope = false;

	if (!DesignatedTarget)
	{
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	float TargetRange = FVector::Dist(OwnerActor->GetActorLocation(), DesignatedTarget->GetActorLocation());
	float ClosureRate = 0.0f;

	if (UAircraftRadarComponent* Radar = GetRadarComponent())
	{
		FRadarTrack Track;
		if (Radar->GetTrackByActor(DesignatedTarget, Track))
		{
			TargetRange = Track.Range;
			ClosureRate = Track.ClosureRate;
		}
	}

	FStationStore Store;
	if (!GetActiveStoreForStation(StationIndex, Store))
	{
		return false;
	}

	if (!IsValid(Store.MountedActor))
	{
		return false;
	}

	float BaseCruiseSpeed = 100000.0f; // 1000 m/s default (~Mach 3)
	float MotorBurnTime = 5.0f;
	float MaxFlightTime = 35.0f;

	if (const UMissileGuidanceComponent* Guidance = Store.MountedActor->FindComponentByClass<UMissileGuidanceComponent>())
	{
		BaseCruiseSpeed = Guidance->MaxCruiseSpeed;
		MaxFlightTime = 40.0f;
	}

	// Rmin: Minimum arming / motor ignition distance (~1.5 km)
	OutRmin = FMath::Max(150000.0f, BaseCruiseSpeed * 1.5f);

	// Rne (No-Escape): Range against a maneuvering beaming target
	const float ClosureBoost = ClosureRate * 15.0f;
	OutRne = FMath::Max(OutRmin * 1.5f, (BaseCruiseSpeed * MotorBurnTime * 2.5f) + (ClosureBoost * 0.5f));

	// Rmax (Aerodynamic Max Range): Maximum kinematic glide range
	OutRmax = FMath::Max(OutRne * 1.3f, (BaseCruiseSpeed * MaxFlightTime * 0.5f) + ClosureBoost);

	OutInShootingEnvelope = (TargetRange >= OutRmin && TargetRange <= OutRmax);
	return true;
}

bool UModularMissionManagement::StepDesignatedRadarTrack(bool bForward)
{
	if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
	{
		if (Radar->CycleTargetDesignation(bForward))
		{
			const int32 BuggedID = Radar->GetBuggedTrackID();
			FRadarTrack Track;
			if (Radar->GetTrackByID(BuggedID, Track) && Track.TrackedActor.IsValid())
			{
				SetDesignatedTarget(Track.TrackedActor.Get());
				return true;
			}
		}
	}
	return false;
}

bool UModularMissionManagement::SelectDogfightStation()
{
	// Priority 1: Internal Cannon
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::InternalCannon && Station.GetCurrentAmmo() > 0)
		{
			return SelectStation(Station.StationIndex);
		}
	}

	// Priority 2: Heat-seeking short-range IR missile (Sidewinder / Archer)
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::AirToAirMissile_IR && Station.GetCurrentAmmo() > 0)
		{
			return SelectStation(Station.StationIndex);
		}
	}

	// Priority 3: Gun Pod
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::GunPod && Station.GetCurrentAmmo() > 0)
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
		if (Station.StoreType == EStoreType::AirToAirMissile_Radar && Station.GetCurrentAmmo() > 0)
		{
			return SelectStation(Station.StationIndex);
		}
	}

	// Priority 2: IR missile fallback
	for (const FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::AirToAirMissile_IR && Station.GetCurrentAmmo() > 0)
		{
			return SelectStation(Station.StationIndex);
		}
	}

	return false;
}

void UModularMissionManagement::HandleDesignatedTargetDestroyed(AActor* DestroyedActor)
{
	// Safely clear target if the destroyed actor was our target or our current target is no longer valid
	if (DestroyedActor == DesignatedTarget || !IsValid(DesignatedTarget))
	{
		ClearDesignatedTarget();
	}
}

bool UModularMissionManagement::CanFire(int32 StationIndex, EWeaponLaunchFailureReason& OutFailReason) const
{
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

	const FWeaponStation& Station = Stations[ArrayIndex];

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

	if (Station.GetCurrentAmmo() <= 0 || Station.Status == EStationStatus::Empty)
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
		for (const FStationStore& Store : Station.Stores)
		{
			if (Store.Status == EStoreStatus::Ready && IsValid(Store.MountedActor))
			{
				if (UMasterWeaponComponent* WeaponComp = Store.MountedActor->FindComponentByClass<UMasterWeaponComponent>())
				{
					if (!WeaponComp->CanFireWeapon())
					{
						OutFailReason = EWeaponLaunchFailureReason::WeaponNotReady;
						return false;
					}
				}
				break; // Only check the next ready store
			}
		}
	}

	OutFailReason = EWeaponLaunchFailureReason::None;
	return true;
}

bool UModularMissionManagement::Fire(int32 StationIndex)
{
	EWeaponLaunchFailureReason FailReason;
	if (!CanFire(StationIndex, FailReason))
	{
		OnWeaponLaunchFailed.Broadcast(StationIndex, FailReason);
		return false;
	}

	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}

	FWeaponStation& Station = Stations[ArrayIndex];
	AActor* TargetToUse = DesignatedTarget.Get();

	// If station is an internal cannon, execute a single gun burst cycle
	if (Station.StoreType == EStoreType::InternalCannon)
	{
		ProcessGunFireCycle(StationIndex);
		OnWeaponFired.Broadcast(StationIndex, nullptr, Station.StoreType, Station.GetCurrentAmmo());
		return true;
	}

	// If in SIMULATE mode, execute dry fire without consuming weapon or physics
	if (MasterArmMode == EMasterArmMode::Simulate)
	{
		OnWeaponFired.Broadcast(StationIndex, nullptr, Station.StoreType, Station.GetCurrentAmmo());
		return true;
	}

	// Live Release
	AActor* ReleasedActor = ExecuteStoreRelease(Station, TargetToUse);

	if (Station.bUsesAmmoManagement)
	{
		Station.CurrentAmmo = FMath::Max(0, Station.CurrentAmmo - 1);
	}

	if (Station.GetCurrentAmmo() == 0)
	{
		Station.Status = EStationStatus::Empty;
	}

	// Auto-step station logic to maintain balance
	if (bAutoStepStationOnFire)
	{
		if (Station.SymmetricStationIndex > 0)
		{
			FWeaponStation SymStation;
			if (GetStation(Station.SymmetricStationIndex, SymStation) && SymStation.GetCurrentAmmo() > 0)
			{
				SelectStation(Station.SymmetricStationIndex);
			}
			else
			{
				SelectNextStationOfStoreType(Station.StoreType);
			}
		}
		else
		{
			SelectNextStationOfStoreType(Station.StoreType);
		}
	}
	else
	{
		// If we didn't change stations, ensure the next weapon on this same rack is activated
		if (MasterMode != EAircraftMasterMode::Navigation)
		{
			if (UMasterWeaponComponent* NextWeapon = GetActiveWeaponComponent(SelectedStationIndex))
			{
				ProgramWeaponIgnoreList(NextWeapon);
				NextWeapon->ActivateWeapon(true);
			}
		}
	}

	// Broadcast events after all state changes and auto-stepping are complete
	OnWeaponFired.Broadcast(StationIndex, ReleasedActor, Station.StoreType, Station.GetCurrentAmmo());
	OnStoresInventoryChanged.Broadcast();

	return true;
}

void UModularMissionManagement::StartFiring(int32 StationIndex)
{
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		OnWeaponLaunchFailed.Broadcast(StationIndex, EWeaponLaunchFailureReason::StationNotFound);
		return;
	}

	FWeaponStation& Station = Stations[ArrayIndex];
	if (Station.StoreType != EStoreType::InternalCannon)
	{
		// Safely abort if called on non-cannon stations
		UE_LOG(LogTemp, Warning, TEXT("UModularMissionManagement::StartFiring: Station %d is not configured as an InternalCannon! Aborting."), StationIndex);
		return;
	}

	if (ActiveGunTimers.Contains(StationIndex))
	{
		// Already firing on this station
		return;
	}

	EWeaponLaunchFailureReason FailReason;
	if (!CanFire(StationIndex, FailReason))
	{
		OnWeaponLaunchFailed.Broadcast(StationIndex, FailReason);
		return;
	}

	// Calculate fire interval from RPM
	const float SafeRPM = FMath::Max(60.0f, Station.RateOfFireRPM);
	const float FireInterval = 60.0f / SafeRPM;

	// Broadcast lightweight trigger start event
	OnGunFiringStarted.Broadcast(StationIndex);

	// Fire initial shot immediately
	ProcessGunFireCycle(StationIndex);

	// Set recurring timer for continuous fire if still has ammo
	if (Station.CurrentAmmo > 0)
	{
		FTimerDelegate TimerDel;
		TimerDel.BindUObject(this, &UModularMissionManagement::ProcessGunFireCycle, StationIndex);

		FTimerHandle NewHandle;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(NewHandle, TimerDel, FireInterval, true);
			ActiveGunTimers.Add(StationIndex, NewHandle);
		}
	}
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
	return ActiveGunTimers.Contains(StationIndex);
}

bool UModularMissionManagement::IsAnyGunFiring() const
{
	return ActiveGunTimers.Num() > 0;
}

void UModularMissionManagement::ProcessGunFireCycle(int32 StationIndex)
{
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		StopFiring(StationIndex);
		return;
	}

	FWeaponStation& Station = Stations[ArrayIndex];
	if (Station.StoreType != EStoreType::InternalCannon)
	{
		StopFiring(StationIndex);
		return;
	}

	// Verify Master Arm
	if (MasterArmMode == EMasterArmMode::Safe)
	{
		StopFiring(StationIndex);
		OnWeaponLaunchFailed.Broadcast(StationIndex, EWeaponLaunchFailureReason::MasterArmSafe);
		return;
	}

	if (Station.Status == EStationStatus::Fault)
	{
		StopFiring(StationIndex);
		OnWeaponLaunchFailed.Broadcast(StationIndex, EWeaponLaunchFailureReason::StationFault);
		return;
	}

	if (Station.CurrentAmmo <= 0 || Station.GunMuzzles.Num() == 0)
	{
		StopFiring(StationIndex);
		Station.Status = EStationStatus::Empty;
		OnWeaponLaunchFailed.Broadcast(StationIndex, EWeaponLaunchFailureReason::AmmoDepleted);
		OnStoresInventoryChanged.Broadcast();
		return;
	}

	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		StopFiring(StationIndex);
		return;
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
		OnWeaponLaunchFailed.Broadcast(StationIndex, EWeaponLaunchFailureReason::NoWeaponConfigured);
		return;
	}

	for (int32 MuzzleIdx : MuzzlesToFire)
	{
		if (Station.CurrentAmmo <= 0)
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
		if (MasterArmMode != EMasterArmMode::Simulate)
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

		Station.CurrentAmmo = FMath::Max(0, Station.CurrentAmmo - 1);
	}

	if (Station.CurrentAmmo == 0)
	{
		Station.Status = EStationStatus::Empty;
		StopFiring(StationIndex);
		OnStoresInventoryChanged.Broadcast();
	}
}

void UModularMissionManagement::SimulateTracedBullets(float DeltaTime)
{
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

	for (int32 i = ActiveTracedBullets.Num() - 1; i >= 0; --i)
	{
		FActiveTracedBullet& Bullet = ActiveTracedBullets[i];
		Bullet.RemainingLifetime -= DeltaTime;
		if (Bullet.RemainingLifetime <= 0.0f)
		{
			ActiveTracedBullets.RemoveAtSwap(i);
			continue;
		}

		// Apply aerodynamic drag deceleration: a = -v_hat * (v^2 * Cd)
		if (Bullet.DragCoefficient > 0.0f)
		{
			const float SpeedSq = Bullet.Velocity.SizeSquared();
			const FVector DragForce = -Bullet.Velocity.GetSafeNormal() * (SpeedSq * Bullet.DragCoefficient);
			Bullet.Velocity += DragForce * DeltaTime;
		}

		// Apply gravity drop
		if (Bullet.GravityScale > 0.0f)
		{
			const FVector GravityAccel(0.0f, 0.0f, -980.0f * Bullet.GravityScale);
			Bullet.Velocity += GravityAccel * DeltaTime;
		}

		const FVector NextPosition = Bullet.CurrentPosition + (Bullet.Velocity * DeltaTime);

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

			// Broadcast hit result to user logic (no hardcoded damage applied)
			OnGunBulletHit.Broadcast(Bullet.StationIndex, Bullet.MuzzleIndex, HitResult);

			ActiveTracedBullets.RemoveAtSwap(i);
		}
		else
		{
			if (bDebug)
			{
				FAircraftCombatDebug::DrawBulletTracer(World, Bullet.CurrentPosition, NextPosition, DeltaTime);
			}

			Bullet.CurrentPosition = NextPosition;
		}
	}
}

AActor* UModularMissionManagement::ExecuteStoreRelease(FWeaponStation& Station, AActor* TargetActor)
{
	AActor* FiredActor = nullptr;
	FStationStore* ActiveStore = nullptr;

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
		}
	}

	if (!IsValid(FiredActor))
	{
		return nullptr;
	}

	// Detach physically from aircraft if not using ammo management
	if (!Station.bUsesAmmoManagement)
	{
		FiredActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}

	SetupStoreCollisionIgnores(FiredActor);

	if (AWeapon* Weapon = Cast<AWeapon>(FiredActor))
	{
		Weapon->SetMounted(false, GetOwner());
	}

	USceneComponent* MeshComp = ResolveAircraftMesh();

	// Handle weapon launch sequence
	if (UMasterWeaponComponent* WeaponComp = FiredActor->FindComponentByClass<UMasterWeaponComponent>())
	{
		WeaponComp->InitializeWeapon(Cast<APawn>(GetOwner()));
		ProgramWeaponIgnoreList(WeaponComp);
		
		if (UMissileGuidanceComponent* Guidance = Cast<UMissileGuidanceComponent>(WeaponComp))
		{
			if (UIRMissileGuidanceComponent* IRGuidance = Cast<UIRMissileGuidanceComponent>(Guidance))
			{
				if (TargetActor)
				{
					IRGuidance->SlaveToDesignatedTarget(TargetActor);
				}
			}
			else
			{
				if (TargetActor)
				{
					Guidance->LockMissile(TargetActor);
				}
			}

			// If this is a radar missile, connect it to the aircraft radar for datalink updates and initial kinematics
			if (URadarMissileGuidanceComponent* RadarGuidance = Cast<URadarMissileGuidanceComponent>(Guidance))
			{
				if (UAircraftRadarComponent* Radar = ResolveRadarComponent())
				{
					RadarGuidance->SetParentRadar(Radar);

					FRadarTrack TrackInfo;
					if (TargetActor && Radar->GetTrackByActor(TargetActor, TrackInfo))
					{
						RadarGuidance->SetInertialTarget(TrackInfo.LastKnownPosition, TrackInfo.EstimatedVelocity);
					}
					else if (TargetActor)
					{
						RadarGuidance->SetInertialTarget(TargetActor->GetActorLocation(), TargetActor->GetVelocity());
					}
				}
				else if (TargetActor)
				{
					RadarGuidance->SetInertialTarget(TargetActor->GetActorLocation(), TargetActor->GetVelocity());
				}
			}
		}
		
		WeaponComp->ActivateWeapon(true);
		
		FTransform ReleaseTransform = GetOwner()->GetActorTransform();
		if (MeshComp && ActiveStore->SocketName != NAME_None && MeshComp->DoesSocketExist(ActiveStore->SocketName))
		{
			ReleaseTransform = MeshComp->GetSocketTransform(ActiveStore->SocketName);
		}
		const FVector WorldImpulse = ReleaseTransform.TransformVector(Station.EjectionImpulse);
		WeaponComp->ApplyEjectionImpulse(WorldImpulse);
		
		WeaponComp->FireWeapon();
	}
	else if (!Station.bUsesAmmoManagement)
	{
		// Non-guided ordnance or ballistic bomb release
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
		const FVector WorldImpulse = ReleaseTransform.TransformVector(Station.EjectionImpulse);

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

	if (!Station.bUsesAmmoManagement)
	{
		ActiveStore->MountedActor = nullptr;
		ActiveStore->Status = EStoreStatus::Fired;
	}

	return FiredActor;
}



int32 UModularMissionManagement::EmergencyJettisonAll()
{
	int32 JettisonCount = 0;

	for (FWeaponStation& Station : Stations)
	{
		if (Station.bCanJettison && Station.GetCurrentAmmo() > 0)
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

	USceneComponent* MeshComp = ResolveAircraftMesh();
	bool bJettisonedAny = false;

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
			const FVector WorldImpulse = JettisonTransform.TransformVector(Station.EjectionImpulse);

			if (UMasterWeaponComponent* WeaponComp = StoreActor->FindComponentByClass<UMasterWeaponComponent>())
			{
				WeaponComp->ApplyEjectionImpulse(WorldImpulse);
				WeaponComp->DetachWeapon();
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
		return false;
	}

	Station.Status = EStationStatus::Jettisoned;

	OnWeaponJettisoned.Broadcast(StationIndex, Station.StoreType);
	OnStoresInventoryChanged.Broadcast();

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
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}

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

	SpawnStoreForStation(StationIndex);
	OnStoresInventoryChanged.Broadcast();
	return true;
}

bool UModularMissionManagement::ReloadStation(int32 StationIndex, int32 AmmoCount)
{
	const int32 ArrayIndex = FindStationArrayIndex(StationIndex);
	if (ArrayIndex == INDEX_NONE)
	{
		return false;
	}

	FWeaponStation& Station = Stations[ArrayIndex];
	
	if (Station.StoreType == EStoreType::InternalCannon || Station.bUsesAmmoManagement)
	{
		Station.CurrentAmmo = FMath::Clamp(AmmoCount, 0, Station.MaxAmmo);
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
				Station.Stores[i].Status = EStoreStatus::Fired;
			}
		}
	}

	Station.Status = (Station.GetCurrentAmmo() > 0) ? ((Station.StationIndex == SelectedStationIndex) ? EStationStatus::Selected : EStationStatus::Ready) : EStationStatus::Empty;
	SpawnStoreForStation(StationIndex);

	OnStoresInventoryChanged.Broadcast();
	return true;
}

void UModularMissionManagement::ReloadAllStations()
{
	for (FWeaponStation& Station : Stations)
	{
		if (Station.StoreType == EStoreType::InternalCannon || Station.bUsesAmmoManagement)
		{
			Station.CurrentAmmo = Station.MaxAmmo;
		}
		else
		{
			for (int32 i = 0; i < Station.Stores.Num(); i++)
			{
				Station.Stores[i].Status = EStoreStatus::Ready;
			}
		}
		
		Station.Status = (Station.StationIndex == SelectedStationIndex) ? EStationStatus::Selected : EStationStatus::Ready;
		SpawnStoreForStation(Station.StationIndex);
	}

	OnStoresInventoryChanged.Broadcast();
}
