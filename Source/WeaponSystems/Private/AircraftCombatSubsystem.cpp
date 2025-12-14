// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#include "AircraftCombatSubsystem.h"
#include "AircraftRadarComponent.h"
#include "RadarMissileGuidanceComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UAircraftCombatSubsystem::UAircraftCombatSubsystem()
{
}

void UAircraftCombatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisteredRadars.Reset();
	RegisteredMissiles.Reset();
	RegisteredCombatActors.Reset();
}

void UAircraftCombatSubsystem::Deinitialize()
{
	RegisteredRadars.Reset();
	RegisteredMissiles.Reset();
	RegisteredCombatActors.Reset();
	Super::Deinitialize();
}

UAircraftCombatSubsystem* UAircraftCombatSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UAircraftCombatSubsystem>() : nullptr;
}

void UAircraftCombatSubsystem::RegisterRadar(UAircraftRadarComponent* Radar)
{
	if (!Radar)
	{
		return;
	}

	PruneStaleRegistrations();

	for (const auto& Entry : RegisteredRadars)
	{
		if (Entry.Get() == Radar)
		{
			return; // Already registered
		}
	}

	RegisteredRadars.Add(Radar);
}

void UAircraftCombatSubsystem::UnregisterRadar(UAircraftRadarComponent* Radar)
{
	if (!Radar)
	{
		return;
	}

	RegisteredRadars.RemoveAll([Radar](const TWeakObjectPtr<UAircraftRadarComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Radar;
	});
}

void UAircraftCombatSubsystem::GetRadarsInRange(const FVector& Location, float MaxRange, TArray<UAircraftRadarComponent*>& OutRadars) const
{
	OutRadars.Reset();
	const float MaxRangeSq = FMath::Square(MaxRange);

	for (const auto& WeakRadar : RegisteredRadars)
	{
		if (UAircraftRadarComponent* Radar = WeakRadar.Get())
		{
			if (!Radar->IsRadarEmitting())
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(Location, Radar->GetRadarLocation());
			if (DistSq <= MaxRangeSq)
			{
				OutRadars.Add(Radar);
			}
		}
	}
}

void UAircraftCombatSubsystem::RegisterMissileSeeker(URadarMissileGuidanceComponent* Missile)
{
	if (!Missile)
	{
		return;
	}

	PruneStaleRegistrations();

	for (const auto& Entry : RegisteredMissiles)
	{
		if (Entry.Get() == Missile)
		{
			return;
		}
	}

	RegisteredMissiles.Add(Missile);
}

void UAircraftCombatSubsystem::UnregisterMissileSeeker(URadarMissileGuidanceComponent* Missile)
{
	if (!Missile)
	{
		return;
	}

	RegisteredMissiles.RemoveAll([Missile](const TWeakObjectPtr<URadarMissileGuidanceComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Missile;
	});
}

void UAircraftCombatSubsystem::GetActiveMissileSeekersInRange(const FVector& Location, float MaxRange, TArray<URadarMissileGuidanceComponent*>& OutMissiles) const
{
	OutMissiles.Reset();
	const float MaxRangeSq = FMath::Square(MaxRange);

	for (const auto& WeakMissile : RegisteredMissiles)
	{
		if (URadarMissileGuidanceComponent* Missile = WeakMissile.Get())
		{
			// Only return missiles whose onboard radar seeker is actively radiating RF energy into space (Pitbull / Maddog)
			if (!Missile->IsSeekerEmittingRF())
			{
				continue;
			}

			if (const AActor* Owner = Missile->GetOwner())
			{
				const float DistSq = FVector::DistSquared(Location, Owner->GetActorLocation());
				if (DistSq <= MaxRangeSq)
				{
					OutMissiles.Add(Missile);
				}
			}
		}
	}
}

void UAircraftCombatSubsystem::RegisterCombatActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	PruneStaleRegistrations();

	for (const auto& Entry : RegisteredCombatActors)
	{
		if (Entry.Get() == Actor)
		{
			return;
		}
	}

	RegisteredCombatActors.Add(Actor);
}

void UAircraftCombatSubsystem::UnregisterCombatActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	RegisteredCombatActors.RemoveAll([Actor](const TWeakObjectPtr<AActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Actor;
	});
}

void UAircraftCombatSubsystem::GetCombatActorsInVolume(const FVector& Origin, float MaxRange, TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	const float MaxRangeSq = FMath::Square(MaxRange);

	for (const auto& WeakActor : RegisteredCombatActors)
	{
		if (AActor* Actor = WeakActor.Get())
		{
			const float DistSq = FVector::DistSquared(Origin, Actor->GetActorLocation());
			if (DistSq <= MaxRangeSq)
			{
				OutActors.Add(Actor);
			}
		}
	}
}

void UAircraftCombatSubsystem::GetCombatPawnsInRange(const FVector& Location, float MaxRange, TArray<APawn*>& OutPawns) const
{
	OutPawns.Reset();
	TArray<AActor*> Actors;
	GetCombatActorsInVolume(Location, MaxRange, Actors);
	for (AActor* A : Actors)
	{
		if (APawn* P = Cast<APawn>(A))
		{
			OutPawns.Add(P);
		}
	}
}

void UAircraftCombatSubsystem::PruneStaleRegistrations()
{
	RegisteredRadars.RemoveAll([](const TWeakObjectPtr<UAircraftRadarComponent>& Entry) { return !Entry.IsValid(); });
	RegisteredMissiles.RemoveAll([](const TWeakObjectPtr<URadarMissileGuidanceComponent>& Entry) { return !Entry.IsValid(); });
	RegisteredCombatActors.RemoveAll([](const TWeakObjectPtr<AActor>& Entry) { return !Entry.IsValid(); });
}
