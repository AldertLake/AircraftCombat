// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

//
// CombatTeamUtility.cpp — Central IFF resolution via IGenericTeamAgentInterface
//

#include "CombatTeamUtility.h"
#include "IFFTransponderComponent.h"
#include "AircraftRadarComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

ETeamAttitude::Type FCombatTeamUtility::GetAttitude(
	const AActor* Observer,
	const AActor* Target,
	ETeamAttitude::Type Fallback)
{
	if (!Observer || !Target || Observer == Target)
	{
		return ETeamAttitude::Friendly;
	}

	const IGenericTeamAgentInterface* ObserverAgent = ResolveTeamAgent(Observer);
	if (!ObserverAgent)
	{
		return Fallback;
	}

	const IGenericTeamAgentInterface* TargetAgent = ResolveTeamAgent(Target);
	if (!TargetAgent)
	{
		return Fallback;
	}

	return ObserverAgent->GetTeamAttitudeTowards(*Target);
}

bool FCombatTeamUtility::IsFriendly(const AActor* Observer, const AActor* Target)
{
	return GetAttitude(Observer, Target) == ETeamAttitude::Friendly;
}

bool FCombatTeamUtility::IsHostile(const AActor* Observer, const AActor* Target)
{
	return GetAttitude(Observer, Target) == ETeamAttitude::Hostile;
}

const IGenericTeamAgentInterface* FCombatTeamUtility::ResolveTeamAgent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	// 1. Try the actor directly (e.g. Pawn, Vehicle, or Controller C++ class implementing the interface)
	const IGenericTeamAgentInterface* Agent = Cast<IGenericTeamAgentInterface>(Actor);
	if (Agent)
	{
		return Agent;
	}

	// 2. Check for explicit transponder component (Tanks, Soldiers, SAMs, Civilian Props)
	if (const UIFFTransponderComponent* Transponder = Actor->FindComponentByClass<UIFFTransponderComponent>())
	{
		return Transponder;
	}

	// 3. Check for airborne radar component (Radar-equipped aircraft & SAM radars act as their own IFF agent)
	if (const UAircraftRadarComponent* Radar = Actor->FindComponentByClass<UAircraftRadarComponent>())
	{
		return Radar;
	}

	// 4. If the actor is a Controller, check its PlayerState
	if (const AController* Controller = Cast<AController>(Actor))
	{
		Agent = Cast<IGenericTeamAgentInterface>(Controller->PlayerState.Get());
		if (Agent)
		{
			return Agent;
		}
	}

	// 5. If the actor is a Pawn, check its Controller and PlayerState (essential for multiplayer clients)
	if (const APawn* AsPawn = Cast<APawn>(Actor))
	{
		// Try Controller (valid on Server and locally-controlled Autonomous Proxy)
		if (const AController* Controller = AsPawn->GetController())
		{
			Agent = Cast<IGenericTeamAgentInterface>(Controller);
			if (Agent)
			{
				return Agent;
			}
			Agent = Cast<IGenericTeamAgentInterface>(Controller->PlayerState.Get());
			if (Agent)
			{
				return Agent;
			}
		}

		// Try PlayerState on the Pawn (replicated to ALL clients, enabling IFF on simulated proxies)
		Agent = Cast<IGenericTeamAgentInterface>(AsPawn->GetPlayerState());
		if (Agent)
		{
			return Agent;
		}
	}

	return nullptr;
}

ERadarIFFResult FCombatTeamUtility::AttitudeToIFF(ETeamAttitude::Type Attitude)
{
	switch (Attitude)
	{
	case ETeamAttitude::Friendly:
		return ERadarIFFResult::Friendly;
	case ETeamAttitude::Hostile:
		return ERadarIFFResult::Hostile;
	case ETeamAttitude::Neutral:
		return ERadarIFFResult::Neutral;
	default:
		return ERadarIFFResult::Unknown;
	}
}

ETeamAttitude::Type FCombatTeamUtility::UnknownAttitudeToTeamAttitude(EIFFUnknownAttitude InAttitude)
{
	switch (InAttitude)
	{
	case EIFFUnknownAttitude::Hostile:
		return ETeamAttitude::Hostile;
	case EIFFUnknownAttitude::Friendly:
		return ETeamAttitude::Friendly;
	case EIFFUnknownAttitude::Neutral:
	default:
		return ETeamAttitude::Neutral;
	}
}
