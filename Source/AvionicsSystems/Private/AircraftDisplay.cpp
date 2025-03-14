// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftDisplay.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "DisplayComponent.h"
#include "ModularMissionManagement.h"
#include "AircraftRadarComponent.h"
#include "RadarWarningReceiverComponent.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

void UAircraftDisplay::NativeConstruct()
{
	Super::NativeConstruct();

	if (bIndependentMode && !bIsDisplayInitialized && PlayerAircraft)
	{
		NativeInitialization();
	}
}

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

	if (!bIndependentMode && !DisplayComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("UAircraftDisplay: DisplayComponent is invalid / nullptr on %s!"), *GetNameSafe(GetTypedOuter<AActor>()));
		return;
	}

	// Auto-discover avionics and weapon components on the player aircraft all at once (skip if not present)
	if (!ModularMissionManagement)
	{
		ModularMissionManagement = PlayerAircraft->FindComponentByClass<UModularMissionManagement>();
	}

	if (!AircraftRadar)
	{
		AircraftRadar = PlayerAircraft->FindComponentByClass<UAircraftRadarComponent>();
	}

	if (!RadarWarningReceiver)
	{
		RadarWarningReceiver = PlayerAircraft->FindComponentByClass<URadarWarningReceiverComponent>();
	}

	bIsDisplayInitialized = true;
	OnDisplayInitialized();

	if (bIndependentMode)
	{
		OnSOIStateChanged(bLocalSensorOfInterest);
	}
}

void UAircraftDisplay::AssignLockedTarget(AActor* InLockedTarget)
{
	LockedTarget = InLockedTarget;
}

bool UAircraftDisplay::IsSymbolInDisplayBoundarie(UWidget* SymbolWidget, FVector2D XLimits, FVector2D YLimits) const
{
	if (!SymbolWidget)
	{
		return false;
	}

	const FVector2D Translation = SymbolWidget->GetRenderTransform().Translation;

	const float MinX = FMath::Min(XLimits.X, XLimits.Y);
	const float MaxX = FMath::Max(XLimits.X, XLimits.Y);
	const float MinY = FMath::Min(YLimits.X, YLimits.Y);
	const float MaxY = FMath::Max(YLimits.X, YLimits.Y);

	return (Translation.X >= MinX && Translation.X <= MaxX &&
	        Translation.Y >= MinY && Translation.Y <= MaxY);
}

bool UAircraftDisplay::IsSymbolInHUD(UWidget* SymbolWidget) const
{
	return IsSymbolInDisplayBoundarie(SymbolWidget, FVector2D(-100.0f, 100.0f), FVector2D(-100.0f, 100.0f));
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

bool UAircraftDisplay::GetWorldWidgetPosition(const FVector& TargetLocation,
	const FRadarDisplayProjection& Projection, FVector2D& OutWidgetPosition)
{
	return FRadarDisplayGeometryMath::Project(TargetLocation, Projection, OutWidgetPosition);
}

void UAircraftDisplay::SetSensorOfInterest(bool bEnable)
{
	if (DisplayComponent)
	{
		DisplayComponent->SetSensorOfInterest(bEnable);
	}
	else
	{
		if (bLocalSensorOfInterest != bEnable)
		{
			bLocalSensorOfInterest = bEnable;
			OnSOIStateChanged(bEnable);
		}
	}
}

bool UAircraftDisplay::IsSensorOfInterest() const
{
	if (DisplayComponent)
	{
		return DisplayComponent->IsSensorOfInterest();
	}
	return bLocalSensorOfInterest;
}

void UAircraftDisplay::NotifySOIStateChanged(bool bNewState)
{
	bLocalSensorOfInterest = bNewState;
	OnSOIStateChanged(bNewState);
}

