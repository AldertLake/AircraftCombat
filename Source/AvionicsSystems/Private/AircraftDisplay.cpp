// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftDisplay.h"
#include "DisplayComponent.h"
#include "AircraftComponent.h"
#include "Components/Widget.h"
#include "GameFramework/Pawn.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

void UAircraftDisplay::InitializeDisplay(APawn* InPlayerAircraft, UAircraftDisplayComponent* InDisplayComponent)
{
	PlayerAircraft = InPlayerAircraft;
	DisplayComponent = InDisplayComponent;

	NativeInitialization();
}

void UAircraftDisplay::NativeInitialization()
{
	if (!PlayerAircraft)
	{
		UE_LOG(LogTemp, Error, TEXT("UAircraftDisplay: PlayerAircraft is invalid / nullptr on %s!"), *GetNameSafe(GetTypedOuter<AActor>()));
		return;
	}

	if (!DisplayComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("UAircraftDisplay: DisplayComponent is invalid / nullptr on %s!"), *GetNameSafe(GetTypedOuter<AActor>()));
		return;
	}

	bIsDisplayInitialized = true;
	OnDisplayInitialized();
}

void UAircraftDisplay::AssignLockedTarget(AActor* InLockedTarget)
{
	LockedTarget = InLockedTarget;
}

bool UAircraftDisplay::IsSymbolInHUD(UWidget* SymbolWidget) const
{
	if (!SymbolWidget || !SymbolWidget->GetCachedWidget().IsValid())
	{
		return false;
	}

	if (!GEngine || !GEngine->GameViewport)
	{
		return false;
	}

	const FVector2D AbsolutePos = SymbolWidget->GetCachedGeometry().GetAbsolutePosition();

	FVector2D ViewportSize = FVector2D::ZeroVector;
	GEngine->GameViewport->GetViewportSize(ViewportSize);

	return AbsolutePos.X >= 0.0f && AbsolutePos.X <= ViewportSize.X &&
	       AbsolutePos.Y >= 0.0f && AbsolutePos.Y <= ViewportSize.Y;
}

bool UAircraftDisplay::CheckGroundCollision() const
{
	if (!PlayerAircraft || !GetWorld())
	{
		return false;
	}

	const FVector Velocity = PlayerAircraft->GetVelocity();
	const float Speed = Velocity.Size();

	if (Speed < 100.0f)
	{
		return false;
	}

	const FVector Start = PlayerAircraft->GetActorLocation();
	const FVector End = Start + (Velocity.GetSafeNormal() * BreakXTraceDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(PlayerAircraft);

	FHitResult HitResult;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_WorldStatic, QueryParams);

	if (bHit)
	{
		const float HitDistance = HitResult.Distance;
		const float TTI = HitDistance / Speed;
		return TTI <= BreakXTimeThreshold;
	}

	return false;
}
