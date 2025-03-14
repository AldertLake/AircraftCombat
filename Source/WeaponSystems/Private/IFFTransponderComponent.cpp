// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "IFFTransponderComponent.h"
#include "AircraftCombatSubsystem.h"
#include "AircraftRadarComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

UIFFTransponderComponent::UIFFTransponderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UIFFTransponderComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority() &&
		!OwnerActor->FindComponentByClass<UAircraftRadarComponent>())
	{
		if (UAircraftCombatSubsystem* Registry = UAircraftCombatSubsystem::Get(this))
			Registry->RegisterCombatActor(OwnerActor);
	}
}

void UIFFTransponderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority() &&
		!OwnerActor->FindComponentByClass<UAircraftRadarComponent>())
	{
		if (UAircraftCombatSubsystem* Registry = UAircraftCombatSubsystem::Get(this))
			Registry->UnregisterCombatActor(OwnerActor);
	}
	Super::EndPlay(EndPlayReason);
}

void UIFFTransponderComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UIFFTransponderComponent, TeamID);
	DOREPLIFETIME(UIFFTransponderComponent, SquawkCode);
	DOREPLIFETIME(UIFFTransponderComponent, bTransponderActive);
}

void UIFFTransponderComponent::SetTeamID(uint8 NewTeamID)
{
	if (TeamID == NewTeamID)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (OwnerActor && !OwnerActor->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("UIFFTransponderComponent::SetTeamID called on non-authority actor %s. Must be set on server."), *OwnerActor->GetName());
		return;
	}

	TeamID = NewTeamID;
	OnTeamChanged.Broadcast(TeamID);
}

void UIFFTransponderComponent::SetTransponderActive(bool bActive)
{
	if (bTransponderActive == bActive)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (OwnerActor && !OwnerActor->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("UIFFTransponderComponent::SetTransponderActive called on non-authority actor %s. Must be set on server."), *OwnerActor->GetName());
		return;
	}

	bTransponderActive = bActive;
	OnTransponderActiveChanged.Broadcast(bTransponderActive);
}

void UIFFTransponderComponent::SetSquawkCode(int32 NewSquawkCode)
{
	if (SquawkCode == NewSquawkCode)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (OwnerActor && !OwnerActor->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("UIFFTransponderComponent::SetSquawkCode called on non-authority actor %s. Must be set on server."), *OwnerActor->GetName());
		return;
	}

	SquawkCode = NewSquawkCode;
	OnSquawkCodeChanged.Broadcast(SquawkCode);
}

void UIFFTransponderComponent::OnRep_TeamID()
{
	OnTeamChanged.Broadcast(TeamID);
}

void UIFFTransponderComponent::OnRep_SquawkCode()
{
	OnSquawkCodeChanged.Broadcast(SquawkCode);
}

void UIFFTransponderComponent::OnRep_TransponderActive()
{
	OnTransponderActiveChanged.Broadcast(bTransponderActive);
}

void UIFFTransponderComponent::SetGenericTeamId(const FGenericTeamId& InTeamID)
{
	SetTeamID(InTeamID.GetId());
}

FGenericTeamId UIFFTransponderComponent::GetGenericTeamId() const
{
	return FGenericTeamId(TeamID);
}

ETeamAttitude::Type UIFFTransponderComponent::GetTeamAttitudeTowards(const AActor& Other) const
{
	const IGenericTeamAgentInterface* OtherAgent = FCombatTeamUtility::ResolveTeamAgent(&Other);
	if (!OtherAgent)
	{
		return ETeamAttitude::Neutral;
	}

	const FGenericTeamId OtherTeam = OtherAgent->GetGenericTeamId();
	if (OtherTeam == FGenericTeamId::NoTeam || TeamID == FGenericTeamId::NoTeam)
	{
		return ETeamAttitude::Neutral;
	}

	return (OtherTeam.GetId() == TeamID) ? ETeamAttitude::Friendly : ETeamAttitude::Hostile;
}
