// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftRadarComponent.h"
#include "RadarMissileGuidanceComponent.h"

void UAircraftRadarComponent::RegisterLaunchedRadarMissile(URadarMissileGuidanceComponent* Missile)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Missile) ||
		Missile->GetLaunchRadar() != this || !Missile->IsWeaponFired() ||
		!Missile->IsWeaponActivated() || Missile->HasFuzeTriggered()) return;
	PruneLaunchedRadarMissiles();
	for (const FLaunchedMissileEntry& Entry : LaunchedRadarMissiles)
		if (Entry.Missile.Get() == Missile) return;
	FLaunchedMissileEntry& Entry = LaunchedRadarMissiles.AddDefaulted_GetRef();
	Entry.Missile = Missile;
	Entry.ID = NextLaunchedMissileID++;
}

void UAircraftRadarComponent::UnregisterLaunchedRadarMissile(URadarMissileGuidanceComponent* Missile)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	LaunchedRadarMissiles.RemoveAll([Missile](const FLaunchedMissileEntry& Entry)
		{ return !Entry.Missile.IsValid() || Entry.Missile.Get() == Missile; });
}

void UAircraftRadarComponent::PruneLaunchedRadarMissiles()
{
	LaunchedRadarMissiles.RemoveAll([this](const FLaunchedMissileEntry& Entry)
	{
		const URadarMissileGuidanceComponent* Missile = Entry.Missile.Get();
		return !IsValid(Missile) || !IsValid(Missile->GetOwner()) ||
			Missile->GetLaunchRadar() != this || !Missile->IsWeaponFired() ||
			!Missile->IsWeaponActivated() || Missile->HasFuzeTriggered();
	});
}

void UAircraftRadarComponent::BuildLaunchedRadarMissileStatuses(
	TArray<FRadarLaunchedMissileStatus>& OutMissiles) const
{
	OutMissiles.Reset();
	OutMissiles.Reserve(LaunchedRadarMissiles.Num());
	for (const FLaunchedMissileEntry& Entry : LaunchedRadarMissiles)
	{
		const URadarMissileGuidanceComponent* Missile = Entry.Missile.Get();
		if (!IsValid(Missile) || !IsValid(Missile->GetOwner()) ||
			Missile->GetLaunchRadar() != this || !Missile->IsWeaponFired() ||
			!Missile->IsWeaponActivated() || Missile->HasFuzeTriggered()) continue;
		FRadarLaunchedMissileStatus& Status = OutMissiles.AddDefaulted_GetRef();
		Status.MissileID = Entry.ID;
		Missile->FillLaunchedMissileStatus(Status);
	}
}

void UAircraftRadarComponent::GetLaunchedRadarMissiles(
	TArray<FRadarLaunchedMissileStatus>& OutMissiles) const
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		if (!LocalOperatorLink.IsValid())
		{
			OutMissiles.Reset();
			return;
		}
		OutMissiles = ClientLaunchedRadarMissiles;
		for (FRadarLaunchedMissileStatus& Status : OutMissiles)
		{
			if (!IsValid(Status.MissileComponent.Get())) Status.MissileComponent = nullptr;
			if (!IsValid(Status.MissileActor.Get())) Status.MissileActor = nullptr;
			if (!IsValid(Status.LaunchRadar.Get())) Status.LaunchRadar = nullptr;
			if (!IsValid(Status.RelevantRadar.Get())) Status.RelevantRadar = nullptr;
			if (!IsValid(Status.CurrentExternalRadar.Get())) Status.CurrentExternalRadar = nullptr;
			if (!IsValid(Status.LastExternalRadar.Get())) Status.LastExternalRadar = nullptr;
		}
		return;
	}
	BuildLaunchedRadarMissileStatuses(OutMissiles);
}
