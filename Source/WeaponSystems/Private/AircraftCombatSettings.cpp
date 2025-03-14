// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftCombatSettings.h"
#include "Components/ActorComponent.h"

UAircraftCombatSettings::UAircraftCombatSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Aircraft Combat");

	// Default vehicle classification tags
	AirIdentificationTags = { FName("Air"), FName("Aircraft"), FName("Plane"), FName("Helicopter") };
	GroundIdentificationTags = { FName("Ground"), FName("Vehicle"), FName("Tank"), FName("Structure") };
	SeaIdentificationTags = { FName("Sea"), FName("Ship"), FName("Boat"), FName("Naval") };
	MissileIdentificationTags = { FName("Missile"), FName("ARH"), FName("Fox3"), FName("AAM"), FName("SAM") };

	// Default RWR Diamond threat priority rankings (Index 0 = highest danger)
	ThreatTypePriority = {
		ERWRThreatType::MissileLaunch,
		ERWRThreatType::MissileSeeker,
		ERWRThreatType::LockOnRadar,
		ERWRThreatType::TrackingRadar,
		ERWRThreatType::SearchRadar
	};

	ThreatVehicleTypePriority = {
		ERadarTargetDomain::Missile,
		ERadarTargetDomain::Air,
		ERadarTargetDomain::Ground,
		ERadarTargetDomain::Sea
	};
}

const UAircraftCombatSettings* UAircraftCombatSettings::Get()
{
	return GetDefault<UAircraftCombatSettings>();
}

ERadarTargetDomain UAircraftCombatSettings::ResolveTargetDomain(const AActor* Candidate, bool bSearchComponentTags) const
{
	if (!IsValid(Candidate))
	{
		return ERadarTargetDomain::Air;
	}

	TArray<FName> CandidateTags = Candidate->Tags;

	if (bSearchComponentTags)
	{
		TInlineComponentArray<UActorComponent*> Components(Candidate);
		for (const UActorComponent* Comp : Components)
		{
			if (Comp)
			{
				for (const FName& CompTag : Comp->ComponentTags)
				{
					CandidateTags.AddUnique(CompTag);
				}
			}
		}
	}

	// 1. Check for Missile tags
	for (const FName& Tag : CandidateTags)
	{
		if (MissileIdentificationTags.Contains(Tag))
		{
			return ERadarTargetDomain::Missile;
		}
	}

	// 2. Check for Sea / Maritime tags
	for (const FName& Tag : CandidateTags)
	{
		if (SeaIdentificationTags.Contains(Tag))
		{
			return ERadarTargetDomain::Sea;
		}
	}

	// 3. Check for Ground tags
	for (const FName& Tag : CandidateTags)
	{
		if (GroundIdentificationTags.Contains(Tag))
		{
			return ERadarTargetDomain::Ground;
		}
	}

	// 4. Check for Air tags
	for (const FName& Tag : CandidateTags)
	{
		if (AirIdentificationTags.Contains(Tag))
		{
			return ERadarTargetDomain::Air;
		}
	}

	// 5. Default: Untagged candidates default to Air
	return ERadarTargetDomain::Air;
}
