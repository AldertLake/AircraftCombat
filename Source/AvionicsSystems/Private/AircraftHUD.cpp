// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftHUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "AircraftDisplay.h"
#include "DisplayComponent.h"
#include "AircraftComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"


void UAircraftHUD::NativeConstruct()
{
	Super::NativeConstruct();

	CachedFunnelPoints.Reserve(32);
	CachedLeftRailPoints.Reserve(32);
	CachedRightRailPoints.Reserve(32);
	CachedFunnelCanvasPoints.Reserve(32);
	CachedLeftRailCanvasPoints.Reserve(32);
	CachedRightRailCanvasPoints.Reserve(32);
}

void UAircraftHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (PlayerAircraft)
	{
		UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(PlayerAircraft->GetRootComponent());
		FVector RawAngularVel = PrimComp ? PrimComp->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;

		const float DeadZoneRad = FunnelConfig.GetDeadZoneInRadians();
		const float RawSpeed = RawAngularVel.Size();

		if (RawSpeed <= DeadZoneRad)
		{
			RawAngularVel = FVector::ZeroVector;
		}
		else if (DeadZoneRad > 0.0f)
		{
			RawAngularVel = RawAngularVel * ((RawSpeed - DeadZoneRad) / RawSpeed);
		}

		FilteredAngularVelocity = FMath::VInterpTo(FilteredAngularVelocity, RawAngularVel, InDeltaTime, FunnelConfig.AngularInterpSpeed);
	}

	CalculateGunFunnel(MyGeometry);
}

int32 UAircraftHUD::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled
) const
{
	int32 MaxLayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (FunnelConfig.bIsEnabled && CachedLeftRailCanvasPoints.Num() >= 2 && CachedRightRailCanvasPoints.Num() >= 2)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			MaxLayerId,
			AllottedGeometry.ToPaintGeometry(),
			CachedLeftRailCanvasPoints,
			ESlateDrawEffect::None,
			FunnelConfig.Color,
			FunnelConfig.bAntialias,
			FunnelConfig.LineThickness
		);

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			MaxLayerId,
			AllottedGeometry.ToPaintGeometry(),
			CachedRightRailCanvasPoints,
			ESlateDrawEffect::None,
			FunnelConfig.Color,
			FunnelConfig.bAntialias,
			FunnelConfig.LineThickness
		);

		MaxLayerId++;
	}

	return MaxLayerId;
}

void UAircraftHUD::CalculateGunFunnel(const FGeometry& MyGeometry)
{
	if (!FunnelConfig.bIsEnabled || !PlayerAircraft || !PlayerCamera || !DisplayComponent)
	{
		CachedFunnelPoints.Reset();
		CachedLeftRailPoints.Reset();
		CachedRightRailPoints.Reset();
		CachedFunnelCanvasPoints.Reset();
		CachedLeftRailCanvasPoints.Reset();
		CachedRightRailCanvasPoints.Reset();
		return;
	}

	CachedFunnelPoints.Reset();
	CachedLeftRailPoints.Reset();
	CachedRightRailPoints.Reset();
	CachedFunnelCanvasPoints.Reset();
	CachedLeftRailCanvasPoints.Reset();
	CachedRightRailCanvasPoints.Reset();

	const FVector AircraftVelocity = PlayerAircraft->GetVelocity();
	FVector MuzzleLocation = PlayerAircraft->GetActorLocation();
	FVector MuzzleForward = PlayerAircraft->GetActorForwardVector();

	if (MuzzleSocketName != NAME_None)
	{
		if (!CachedMuzzleComponent.IsValid() || !CachedMuzzleComponent->DoesSocketExist(MuzzleSocketName))
		{
			CachedMuzzleComponent = nullptr;

			if (USkeletalMeshComponent* AircraftMesh = PlayerAircraft->GetMesh())
			{
				if (AircraftMesh->DoesSocketExist(MuzzleSocketName))
				{
					CachedMuzzleComponent = AircraftMesh;
				}
			}

			if (!CachedMuzzleComponent.IsValid())
			{
				USceneComponent* RootComp = PlayerAircraft->GetRootComponent();
				if (RootComp && RootComp->DoesSocketExist(MuzzleSocketName))
				{
					CachedMuzzleComponent = RootComp;
				}
				else
				{
					TArray<UMeshComponent*> MeshComponents;
					PlayerAircraft->GetComponents<UMeshComponent>(MeshComponents);
					for (UMeshComponent* MeshComp : MeshComponents)
					{
						if (MeshComp && MeshComp->DoesSocketExist(MuzzleSocketName))
						{
							CachedMuzzleComponent = MeshComp;
							break;
						}
					}
				}
			}
		}

		if (CachedMuzzleComponent.IsValid())
		{
			MuzzleLocation = CachedMuzzleComponent->GetSocketLocation(MuzzleSocketName);
			MuzzleForward = CachedMuzzleComponent->GetSocketRotation(MuzzleSocketName).Vector();
		}
	}

	const FVector V0 = AircraftVelocity + (MuzzleForward * FunnelConfig.MuzzleSpeedCMS);
	const float GravityZ = GetWorld() ? GetWorld()->GetGravityZ() : -980.0f;
	const FVector Gravity(0.0, 0.0, GravityZ);

	const FVector EyeLocation = PlayerCamera->GetComponentLocation();
	const double HalfWingspan = FunnelConfig.TargetWingspanCM * 0.5;

	const int32 SampleCount = FMath::Clamp(FunnelConfig.SampleCount, 4, 32);
	const float MinTOF = FMath::Max(0.001f, FunnelConfig.MinTOF);
	const float MaxTOF = FMath::Max(MinTOF + 0.001f, FunnelConfig.MaxTOF);
	const float TimeStep = (MaxTOF - MinTOF) / FMath::Max(1, SampleCount - 1);

	const FVector2D CanvasCenter = MyGeometry.GetLocalSize() * 0.5;
	const FVector AircraftRight = PlayerAircraft->GetActorRightVector();

	const float DeadZoneRad = FunnelConfig.GetDeadZoneInRadians();
	const float FilteredSpeed = FilteredAngularVelocity.Size();
	const float EffectiveAngularSpeed = (FilteredSpeed > DeadZoneRad) ? (FilteredSpeed - DeadZoneRad) : 0.0f;
	const float AngularSpeed = EffectiveAngularSpeed * FunnelConfig.SweepMultiplier;

	const FVector RotationAxis = (FilteredSpeed > KINDA_SMALL_NUMBER) ? (FilteredAngularVelocity / FilteredSpeed) : FVector::UpVector;
	const FVector CameraForward = PlayerCamera->GetForwardVector();

	for (int32 i = 0; i < SampleCount; ++i)
	{
		const float t = MinTOF + (i * TimeStep);

		const FVector FutureBulletPos = MuzzleLocation + (V0 * t) + (0.5f * Gravity * t * t);

		FVector SweptBulletPos = FutureBulletPos;
		if (FunnelConfig.bEnableKinematicSweep && AngularSpeed > KINDA_SMALL_NUMBER)
		{
			const float RotationAngle = AngularSpeed * t;
			const FQuat KinematicSweep(RotationAxis, -RotationAngle);

			const FVector ToBullet = FutureBulletPos - EyeLocation;
			SweptBulletPos = EyeLocation + KinematicSweep.RotateVector(ToBullet);
		}

		const FVector ToSweptBullet = SweptBulletPos - EyeLocation;

		if (FVector::DotProduct(CameraForward, ToSweptBullet.GetSafeNormal()) <= FunnelConfig.CameraCullDotThreshold)
		{
			continue;
		}

		const FVector LOS = ToSweptBullet.GetSafeNormal();
		const FVector ExpansionAxis = FVector::VectorPlaneProject(AircraftRight, LOS).GetSafeNormal();

		const FVector LeftWorldPos = SweptBulletPos - (ExpansionAxis * HalfWingspan);
		const FVector RightWorldPos = SweptBulletPos + (ExpansionAxis * HalfWingspan);

		FVector2D LeftOffset = FVector2D::ZeroVector;
		FVector2D RightOffset = FVector2D::ZeroVector;

		if (ProjectLocationToHUD(LeftWorldPos, LeftOffset) && ProjectLocationToHUD(RightWorldPos, RightOffset))
		{
			const FVector2D LeftCanvas = CanvasCenter + LeftOffset;
			const FVector2D RightCanvas = CanvasCenter + RightOffset;

			// Center-anchored points (0,0 is center of display) for Blueprint getters and UI element placement
			CachedLeftRailPoints.Add(LeftOffset);
			CachedRightRailPoints.Add(RightOffset);
			CachedFunnelPoints.Add((LeftOffset + RightOffset) * 0.5f);

			// Top-left canvas points for Slate MakeLines rendering
			CachedLeftRailCanvasPoints.Add(LeftCanvas);
			CachedRightRailCanvasPoints.Add(RightCanvas);
			CachedFunnelCanvasPoints.Add((LeftCanvas + RightCanvas) * 0.5f);
		}
	}
}

void UAircraftHUD::NativeInitialization()
{
	if (!PlayerAircraft)
	{
		UE_LOG(LogTemp, Error, TEXT("UAircraftHUD: PlayerAircraft is invalid / nullptr on %s! HUD initialization failed."), *GetNameSafe(GetTypedOuter<AActor>()));
		return;
	}

	UCameraComponent* TargetCamera = nullptr;
	TArray<UCameraComponent*> PawnCameras;
	PlayerAircraft->GetComponents<UCameraComponent>(PawnCameras);

	if (!CameraComponentName.IsNone())
	{
		for (UCameraComponent* Cam : PawnCameras)
		{
			if (Cam && (Cam->GetFName() == CameraComponentName || Cam->ComponentHasTag(CameraComponentName)))
			{
				TargetCamera = Cam;
				break;
			}
		}

		if (!TargetCamera)
		{
			UE_LOG(LogTemp, Warning, TEXT("UAircraftHUD: Camera component with name/tag '%s' was not found on %s! Falling back to first available camera."), *CameraComponentName.ToString(), *GetNameSafe(PlayerAircraft));
		}
	}

	if (!TargetCamera && PawnCameras.Num() > 0)
	{
		TargetCamera = PawnCameras[0];
	}

	if (!TargetCamera)
	{
		UE_LOG(LogTemp, Error, TEXT("UAircraftHUD: No CameraComponent found on %s! HUD initialization failed."), *GetNameSafe(PlayerAircraft));
		return;
	}

	PlayerCamera = TargetCamera;
	CachedMuzzleComponent = nullptr;

	Super::NativeInitialization();
}

bool UAircraftHUD::ProjectLocationToHUD(const FVector& Location, FVector2D& Result) const
{
	if (!PlayerCamera || !DisplayComponent)
	{
		Result = FVector2D::ZeroVector;
		return false;
	}

	const FVector LineStart = PlayerCamera->GetComponentLocation();
	const FVector LineEnd = Location;
	const FVector PlaneOrigin = DisplayComponent->GetComponentLocation();
	const FVector PlaneNormal = DisplayComponent->GetForwardVector();

	float T = 0.0f;
	FVector Intersection = FVector::ZeroVector;
	const bool bIntersects = UKismetMathLibrary::LinePlaneIntersection_OriginNormal(
		LineStart,
		LineEnd,
		PlaneOrigin,
		PlaneNormal,
		T,
		Intersection
	);

	if (!bIntersects || T <= 0.0f)
	{
		Result = FVector2D::ZeroVector;
		return false;
	}

	FTransform HUDTransform = DisplayComponent->GetComponentTransform();
	HUDTransform.SetScale3D(FVector::OneVector);

	const FVector LocalPoint = HUDTransform.InverseTransformPosition(Intersection);
	Result = FVector2D(LocalPoint.Y, LocalPoint.Z) * ProjectionScale;
	return true;
}

bool UAircraftHUD::CalculateBoresightCrossLocation(FVector& Result) const
{
	if (!PlayerAircraft || !PlayerCamera)
	{
		Result = FVector::ZeroVector;
		return false;
	}

	FVector MuzzleForward = PlayerAircraft->GetActorForwardVector();

	if (MuzzleSocketName != NAME_None)
	{
		if (USkeletalMeshComponent* AircraftMesh = PlayerAircraft->GetMesh())
		{
			if (AircraftMesh->DoesSocketExist(MuzzleSocketName))
			{
				MuzzleForward = AircraftMesh->GetSocketRotation(MuzzleSocketName).Vector();
			}
		}
		else if (USceneComponent* RootComp = PlayerAircraft->GetRootComponent())
		{
			if (RootComp->DoesSocketExist(MuzzleSocketName))
			{
				MuzzleForward = RootComp->GetSocketRotation(MuzzleSocketName).Vector();
			}
		}
		else
		{
			TArray<UMeshComponent*> MeshComponents;
			PlayerAircraft->GetComponents<UMeshComponent>(MeshComponents);
			for (UMeshComponent* MeshComp : MeshComponents)
			{
				if (MeshComp && MeshComp->DoesSocketExist(MuzzleSocketName))
				{
					MuzzleForward = MeshComp->GetSocketRotation(MuzzleSocketName).Vector();
					break;
				}
			}
		}
	}

	Result = PlayerCamera->GetComponentLocation() + (MuzzleForward * 1000000.0f);
	return true;
}

bool UAircraftHUD::FindFiringSolution(FVector& Result) const
{
	if (!LockedTarget || !PlayerAircraft)
	{
		Result = FVector::ZeroVector;
		return false;
	}

	FVector MuzzleLoc = PlayerAircraft->GetActorLocation();
	FVector MuzzleForward = PlayerAircraft->GetActorForwardVector();

	if (MuzzleSocketName != NAME_None)
	{
		if (!CachedMuzzleComponent.IsValid() || !CachedMuzzleComponent->DoesSocketExist(MuzzleSocketName))
		{
			CachedMuzzleComponent = nullptr;

			USceneComponent* RootComp = PlayerAircraft->GetRootComponent();
			if (RootComp && RootComp->DoesSocketExist(MuzzleSocketName))
			{
				CachedMuzzleComponent = RootComp;
			}
			else
			{
				TArray<UMeshComponent*> MeshComponents;
				PlayerAircraft->GetComponents<UMeshComponent>(MeshComponents);
				for (UMeshComponent* MeshComp : MeshComponents)
				{
					if (MeshComp && MeshComp->DoesSocketExist(MuzzleSocketName))
					{
						CachedMuzzleComponent = MeshComp;
						break;
					}
				}
			}
		}

		if (CachedMuzzleComponent.IsValid())
		{
			MuzzleLoc = CachedMuzzleComponent->GetSocketLocation(MuzzleSocketName);
			MuzzleForward = CachedMuzzleComponent->GetSocketRotation(MuzzleSocketName).Vector();
		}
	}

	const FVector AircraftVel = PlayerAircraft->GetVelocity();
	const FVector TargetLoc = LockedTarget->GetActorLocation();
	const FVector TargetVel = LockedTarget->GetVelocity();
	const FVector TargetAcceleration = FVector::ZeroVector;

	const double Distance = FVector::Dist(MuzzleLoc, TargetLoc);
	if (Distance < MinFiringRange || Distance > MaxFiringRange || BulletsSpeed <= 0.0f)
	{
		Result = FVector::ZeroVector;
		return false;
	}

	const float TOF = Distance / BulletsSpeed;
	const FVector TargetFutureLoc = TargetLoc + (TargetVel * TOF) + (0.5f * TargetAcceleration * TOF * TOF);
	const FVector BulletFutureLoc = MuzzleLoc + (AircraftVel + (MuzzleForward * BulletsSpeed)) * TOF + FVector(0.0f, 0.0f, -490.0f * TOF * TOF);
	const FVector ErrorOffset = BulletFutureLoc - TargetFutureLoc;

	Result = TargetLoc + ErrorOffset;
	return true;
}

float UAircraftHUD::GetTargetClosureRate() const
{
	if (!LockedTarget || !PlayerAircraft)
	{
		return 0.0f;
	}

	const FVector TargetLoc = LockedTarget->GetActorLocation();
	const FVector AircraftLoc = PlayerAircraft->GetActorLocation();
	const FVector TargetVel = LockedTarget->GetVelocity();
	const FVector AircraftVel = PlayerAircraft->GetVelocity();

	const FVector DirectionToTarget = (TargetLoc - AircraftLoc).GetSafeNormal();
	const FVector RelativeVelocity = AircraftVel - TargetVel;

	return FVector::DotProduct(RelativeVelocity, DirectionToTarget);
}

bool UAircraftHUD::CalculateFlightPathPosition(FVector& Result) const
{
	if (!PlayerAircraft || !PlayerCamera)
	{
		Result = FVector::ZeroVector;
		return false;
	}

	const FVector AircraftVel = PlayerAircraft->GetVelocity();
	const FVector TrajectoryDir = (AircraftVel.SizeSquared() > 100.0f)
		? AircraftVel.GetSafeNormal()
		: PlayerAircraft->GetActorForwardVector();

	Result = PlayerCamera->GetComponentLocation() + (TrajectoryDir * 1000000.0f);
	return true;
}

bool UAircraftHUD::GetHorizonPosition(FVector& Result) const
{
	if (!PlayerAircraft || !PlayerCamera)
	{
		Result = FVector::ZeroVector;
		return false;
	}

	const FVector Forward = PlayerAircraft->GetActorForwardVector();
	const FVector HorizonDir = FVector(Forward.X, Forward.Y, 0.0f).GetSafeNormal();

	Result = PlayerCamera->GetComponentLocation() + (HorizonDir * 1000000.0f);
	return true;
}
