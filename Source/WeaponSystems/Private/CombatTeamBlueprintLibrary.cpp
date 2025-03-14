// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "CombatTeamBlueprintLibrary.h"
#include "IFFTransponderComponent.h"
#include "AircraftRadarComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

// =========================================================================
// 1. GAMEMODE & PLAYER TEAMS
// =========================================================================

void UCombatTeamBlueprintLibrary::SetPlayerTeam(AController* PlayerController, uint8 NewTeamID)
{
	if (!PlayerController)
	{
		return;
	}

	// 1. Set on PlayerState (replicated to all clients / simulated proxies)
	if (APlayerState* PS = PlayerController->PlayerState.Get())
	{
		if (UIFFTransponderComponent* PSTransponder = PS->FindComponentByClass<UIFFTransponderComponent>())
		{
			PSTransponder->SetTeamID(NewTeamID);
		}
		if (IGenericTeamAgentInterface* PSAgent = Cast<IGenericTeamAgentInterface>(PS))
		{
			PSAgent->SetGenericTeamId(FGenericTeamId(NewTeamID));
		}
	}

	// 2. Set on Controller itself if it implements interface or has transponder
	if (UIFFTransponderComponent* CTransponder = PlayerController->FindComponentByClass<UIFFTransponderComponent>())
	{
		CTransponder->SetTeamID(NewTeamID);
	}
	if (IGenericTeamAgentInterface* CAgent = Cast<IGenericTeamAgentInterface>(PlayerController))
	{
		CAgent->SetGenericTeamId(FGenericTeamId(NewTeamID));
	}

	// 3. Set on whatever Pawn or Vehicle the player is currently driving
	if (APawn* ControlledPawn = PlayerController->GetPawn())
	{
		SetActorTeam(ControlledPawn, NewTeamID);
	}
}

uint8 UCombatTeamBlueprintLibrary::GetPlayerTeam(const AController* PlayerController)
{
	return GetActorTeam(PlayerController);
}

// =========================================================================
// 2. VEHICLE POSSESSION
// =========================================================================

void UCombatTeamBlueprintLibrary::OnVehiclePossessed(APawn* Vehicle, AController* DriverController)
{
	if (!Vehicle)
	{
		return;
	}

	const AController* Controller = DriverController ? DriverController : Vehicle->GetController();
	if (Controller)
	{
		const uint8 DriverTeam = GetPlayerTeam(Controller);
		if (DriverTeam != 255)
		{
			SetActorTeam(Vehicle, DriverTeam);
			return;
		}
	}
}

void UCombatTeamBlueprintLibrary::OnVehicleUnpossessed(APawn* Vehicle, uint8 AbandonedTeamID)
{
	if (!Vehicle)
	{
		return;
	}

	SetActorTeam(Vehicle, AbandonedTeamID);
}

// =========================================================================
// 3. COMBAT & FRIENDLY FIRE
// =========================================================================

bool UCombatTeamBlueprintLibrary::IsFriendly(const AActor* ActorA, const AActor* ActorB)
{
	if (!ActorA || !ActorB)
	{
		return false;
	}
	if (ActorA == ActorB)
	{
		return true;
	}

	const IGenericTeamAgentInterface* AgentA = FCombatTeamUtility::ResolveTeamAgent(ActorA);
	const IGenericTeamAgentInterface* AgentB = FCombatTeamUtility::ResolveTeamAgent(ActorB);

	if (AgentA && AgentB)
	{
		return AgentA->GetTeamAttitudeTowards(*ActorB) == ETeamAttitude::Friendly;
	}

	const uint8 TeamA = GetActorTeam(ActorA);
	const uint8 TeamB = GetActorTeam(ActorB);

	if (TeamA == 255 || TeamB == 255)
	{
		return false;
	}

	return TeamA == TeamB;
}

bool UCombatTeamBlueprintLibrary::IsHostile(const AActor* ActorA, const AActor* ActorB)
{
	if (!ActorA || !ActorB || ActorA == ActorB)
	{
		return false;
	}

	const IGenericTeamAgentInterface* AgentA = FCombatTeamUtility::ResolveTeamAgent(ActorA);
	const IGenericTeamAgentInterface* AgentB = FCombatTeamUtility::ResolveTeamAgent(ActorB);

	if (AgentA && AgentB)
	{
		return AgentA->GetTeamAttitudeTowards(*ActorB) == ETeamAttitude::Hostile;
	}

	const uint8 TeamA = GetActorTeam(ActorA);
	const uint8 TeamB = GetActorTeam(ActorB);

	if (TeamA == 255 || TeamB == 255)
	{
		return false;
	}

	return TeamA != TeamB;
}

bool UCombatTeamBlueprintLibrary::IsNeutral(const AActor* ActorA, const AActor* ActorB)
{
	if (!ActorA || !ActorB)
	{
		return true;
	}

	return !IsFriendly(ActorA, ActorB) && !IsHostile(ActorA, ActorB);
}

TEnumAsByte<ETeamAttitude::Type> UCombatTeamBlueprintLibrary::GetTeamAttitude(
	const AActor* Observer,
	const AActor* Target,
	TEnumAsByte<ETeamAttitude::Type> Fallback)
{
	return FCombatTeamUtility::GetAttitude(Observer, Target, Fallback.GetValue());
}

ERadarIFFResult UCombatTeamBlueprintLibrary::GetIFFClassification(const AActor* Observer, const AActor* Target)
{
	if (!Observer || !Target)
	{
		return ERadarIFFResult::Unknown;
	}

	// If target has a transponder and it is turned off (EMCON silent), interrogation produces no response
	if (const UIFFTransponderComponent* Transponder = Target->FindComponentByClass<UIFFTransponderComponent>())
	{
		if (!Transponder->IsTransponderActive())
		{
			return ERadarIFFResult::Unknown;
		}
	}

	const ETeamAttitude::Type Attitude = FCombatTeamUtility::GetAttitude(Observer, Target, ETeamAttitude::Neutral);
	return FCombatTeamUtility::AttitudeToIFF(Attitude);
}

// =========================================================================
// 4. GENERAL ACTORS & PROPS
// =========================================================================

void UCombatTeamBlueprintLibrary::SetActorTeam(AActor* TargetActor, uint8 NewTeamID)
{
	if (!TargetActor)
	{
		return;
	}

	bool bUpdatedAny = false;

	// 1. Transponder
	if (UIFFTransponderComponent* Transponder = TargetActor->FindComponentByClass<UIFFTransponderComponent>())
	{
		Transponder->SetTeamID(NewTeamID);
		bUpdatedAny = true;
	}

	// 2. Radar
	if (UAircraftRadarComponent* Radar = TargetActor->FindComponentByClass<UAircraftRadarComponent>())
	{
		Radar->SetTeamID(NewTeamID);
		bUpdatedAny = true;
	}

	// 3. Direct actor interface
	if (IGenericTeamAgentInterface* Agent = Cast<IGenericTeamAgentInterface>(TargetActor))
	{
		Agent->SetGenericTeamId(FGenericTeamId(NewTeamID));
		bUpdatedAny = true;
	}

	// 4. Controller direct / PlayerState check
	if (AController* AsController = Cast<AController>(TargetActor))
	{
		if (APlayerState* PS = AsController->PlayerState.Get())
		{
			if (UIFFTransponderComponent* PSTransponder = PS->FindComponentByClass<UIFFTransponderComponent>())
			{
				PSTransponder->SetTeamID(NewTeamID);
				bUpdatedAny = true;
			}
			if (IGenericTeamAgentInterface* PSAgent = Cast<IGenericTeamAgentInterface>(PS))
			{
				PSAgent->SetGenericTeamId(FGenericTeamId(NewTeamID));
				bUpdatedAny = true;
			}
		}
	}

	// 5. Pawn Controller / PlayerState
	if (APawn* AsPawn = Cast<APawn>(TargetActor))
	{
		if (AController* Controller = AsPawn->GetController())
		{
			if (IGenericTeamAgentInterface* CAgent = Cast<IGenericTeamAgentInterface>(Controller))
			{
				CAgent->SetGenericTeamId(FGenericTeamId(NewTeamID));
				bUpdatedAny = true;
			}
			if (IGenericTeamAgentInterface* PSAgent = Cast<IGenericTeamAgentInterface>(Controller->PlayerState.Get()))
			{
				PSAgent->SetGenericTeamId(FGenericTeamId(NewTeamID));
				bUpdatedAny = true;
			}
			if (UIFFTransponderComponent* PSTransponder = Controller->PlayerState ? Controller->PlayerState->FindComponentByClass<UIFFTransponderComponent>() : nullptr)
			{
				PSTransponder->SetTeamID(NewTeamID);
				bUpdatedAny = true;
			}
		}
		if (IGenericTeamAgentInterface* PSAgent = Cast<IGenericTeamAgentInterface>(AsPawn->GetPlayerState()))
		{
			PSAgent->SetGenericTeamId(FGenericTeamId(NewTeamID));
			bUpdatedAny = true;
		}
		if (UIFFTransponderComponent* PSTransponder = AsPawn->GetPlayerState() ? AsPawn->GetPlayerState()->FindComponentByClass<UIFFTransponderComponent>() : nullptr)
		{
			PSTransponder->SetTeamID(NewTeamID);
			bUpdatedAny = true;
		}
	}

	if (!bUpdatedAny && TargetActor->HasAuthority())
	{
		UE_LOG(LogTemp, Verbose, TEXT("UCombatTeamBlueprintLibrary::SetActorTeam: Actor %s has no IFF components or team interfaces."), *TargetActor->GetName());
	}
}

uint8 UCombatTeamBlueprintLibrary::GetActorTeam(const AActor* TargetActor)
{
	if (!TargetActor)
	{
		return 255;
	}

	// 1. Direct transponder component check
	if (const UIFFTransponderComponent* Transponder = TargetActor->FindComponentByClass<UIFFTransponderComponent>())
	{
		return Transponder->GetTeamID();
	}

	// 2. Direct radar component check
	if (const UAircraftRadarComponent* Radar = TargetActor->FindComponentByClass<UAircraftRadarComponent>())
	{
		return Radar->GetTeamID();
	}

	// 3. Generic Team Agent check
	if (const IGenericTeamAgentInterface* Agent = FCombatTeamUtility::ResolveTeamAgent(TargetActor))
	{
		return Agent->GetGenericTeamId().GetId();
	}

	// 4. Controller direct / PlayerState check
	if (const AController* AsController = Cast<AController>(TargetActor))
	{
		if (const APlayerState* PS = AsController->PlayerState.Get())
		{
			if (const UIFFTransponderComponent* PSTransponder = PS->FindComponentByClass<UIFFTransponderComponent>())
			{
				return PSTransponder->GetTeamID();
			}
			if (const IGenericTeamAgentInterface* PSAgent = Cast<IGenericTeamAgentInterface>(PS))
			{
				return PSAgent->GetGenericTeamId().GetId();
			}
		}
	}

	// 5. Pawn Controller / PlayerState fallback
	if (const APawn* AsPawn = Cast<APawn>(TargetActor))
	{
		if (const AController* Controller = AsPawn->GetController())
		{
			if (const IGenericTeamAgentInterface* CAgent = Cast<IGenericTeamAgentInterface>(Controller))
			{
				return CAgent->GetGenericTeamId().GetId();
			}
			if (const IGenericTeamAgentInterface* PSAgent = Cast<IGenericTeamAgentInterface>(Controller->PlayerState.Get()))
			{
				return PSAgent->GetGenericTeamId().GetId();
			}
			if (const UIFFTransponderComponent* PSTransponder = Controller->PlayerState ? Controller->PlayerState->FindComponentByClass<UIFFTransponderComponent>() : nullptr)
			{
				return PSTransponder->GetTeamID();
			}
		}
		if (const IGenericTeamAgentInterface* PSAgent = Cast<IGenericTeamAgentInterface>(AsPawn->GetPlayerState()))
		{
			return PSAgent->GetGenericTeamId().GetId();
		}
		if (const UIFFTransponderComponent* PSTransponder = AsPawn->GetPlayerState() ? AsPawn->GetPlayerState()->FindComponentByClass<UIFFTransponderComponent>() : nullptr)
		{
			return PSTransponder->GetTeamID();
		}
	}

	return 255;
}

void UCombatTeamBlueprintLibrary::SetTransponderActive(AActor* TargetActor, bool bActive)
{
	if (!TargetActor)
	{
		return;
	}

	if (UIFFTransponderComponent* Transponder = TargetActor->FindComponentByClass<UIFFTransponderComponent>())
	{
		Transponder->SetTransponderActive(bActive);
	}
}

bool UCombatTeamBlueprintLibrary::IsTransponderActive(const AActor* TargetActor)
{
	if (!TargetActor)
	{
		return false;
	}

	if (const UIFFTransponderComponent* Transponder = TargetActor->FindComponentByClass<UIFFTransponderComponent>())
	{
		return Transponder->IsTransponderActive();
	}

	return false;
}

// =========================================================================
// 5. BACKWARD-COMPATIBLE ALIASES
// =========================================================================

void UCombatTeamBlueprintLibrary::SyncPawnTeamFromController(APawn* Pawn, AController* Controller, uint8 FallbackTeamID)
{
	if (!Pawn)
	{
		return;
	}

	const AController* TargetController = Controller ? Controller : Pawn->GetController();
	if (TargetController)
	{
		const uint8 ControllerTeam = GetActorTeam(TargetController);
		if (ControllerTeam != 255)
		{
			SetActorTeam(Pawn, ControllerTeam);
			return;
		}
	}

	SetActorTeam(Pawn, FallbackTeamID);
}

void UCombatTeamBlueprintLibrary::SetActorTeamID(AActor* TargetActor, uint8 NewTeamID)
{
	SetActorTeam(TargetActor, NewTeamID);
}

uint8 UCombatTeamBlueprintLibrary::GetActorTeamID(const AActor* TargetActor)
{
	return GetActorTeam(TargetActor);
}

ERadarIFFResult UCombatTeamBlueprintLibrary::GetActorIFFResult(const AActor* Observer, const AActor* TargetActor)
{
	return GetIFFClassification(Observer, TargetActor);
}
