// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftCombatDebug.h"
#include "AircraftRadarComponent.h"
#include "RadarWarningReceiverComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"

// HUD telemetry

void FAircraftCombatDebug::PrintRadarTelemetry(int32 LineOffset, const FString& Text, const FColor& Color, float TimeToDisplay)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase_Radar + LineOffset, TimeToDisplay, Color, Text);
	}
}

void FAircraftCombatDebug::PrintRWRTelemetry(int32 LineOffset, const FString& Text, const FColor& Color, float TimeToDisplay)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase_RWR + LineOffset, TimeToDisplay, Color, Text);
	}
}

void FAircraftCombatDebug::PrintRWRTelemetry(int32 ThreatCount, bool bHasLockOn, bool bHasMissileLaunch)
{
	if (GEngine)
	{
		const FColor StatusColor = (bHasMissileLaunch ? FColor::Red : (bHasLockOn ? FColor::Orange : FColor::Yellow));
		GEngine->AddOnScreenDebugMessage(
			KeyBase_RWR,
			0.0f,
			StatusColor,
			FString::Printf(TEXT("[RWR] Threats: %d | Lock-On: %s | Missile: %s"),
				ThreatCount,
				bHasLockOn ? TEXT("YES") : TEXT("NO"),
				bHasMissileLaunch ? TEXT("YES") : TEXT("NO"))
		);
	}
}

void FAircraftCombatDebug::PrintMissileTelemetry(int32 LineOffset, const FString& Text, const FColor& Color, float TimeToDisplay)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase_Missile + LineOffset, TimeToDisplay, Color, Text);
	}
}

void FAircraftCombatDebug::PrintMissileTelemetry(const FString& MissileName, const FString& PhaseName, float ClosureRate, float RangeMeters, bool bSeekerActive, bool bDatalinkActive)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			KeyBase_Missile,
			0.0f,
			FColor::Cyan,
			FString::Printf(TEXT("[%s] Phase: %s | Range: %.0fm | Vc: %.0f m/s"),
				*MissileName, *PhaseName, RangeMeters, ClosureRate / 100.0f)
		);
		GEngine->AddOnScreenDebugMessage(
			KeyBase_Missile + 1,
			0.0f,
			bDatalinkActive ? FColor::Green : FColor::Silver,
			FString::Printf(TEXT("[%s] Seeker: %s | Datalink: %s"),
				*MissileName,
				bSeekerActive ? TEXT("PITBULL/ACTIVE") : TEXT("STANDBY"),
				bDatalinkActive ? TEXT("CONNECTED") : TEXT("NO LINK"))
		);
	}
}

void FAircraftCombatDebug::PrintSMSTelemetry(int32 LineOffset, const FString& Text, const FColor& Color, float TimeToDisplay)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase_SMS + LineOffset, TimeToDisplay, Color, Text);
	}
}

// 3D radar visualization

void FAircraftCombatDebug::DrawRadarFrustum(
	const UWorld* World,
	const FVector& Origin,
	const FRotator& Orientation,
	float AzimuthWidthDeg,
	float ElevationHeightDeg,
	float CenterAzimuthDeg,
	float CenterElevationDeg,
	int32 ElevationBars,
	int32 ActiveBar,
	float VisualRange,
	const FColor& FrustumColor)
{
	if (!World || VisualRange <= 0.0f)
	{
		return;
	}

	const FQuat OwnerQuat = Orientation.Quaternion();
	const float HalfAz = AzimuthWidthDeg * 0.5f;
	const float HalfEl = ElevationHeightDeg * 0.5f;

	// 1. Draw 4 corners and connecting frame of the scan volume frustum
	const FRotator Corners[4] = {
		FRotator(CenterElevationDeg + HalfEl, CenterAzimuthDeg - HalfAz, 0.0f),
		FRotator(CenterElevationDeg + HalfEl, CenterAzimuthDeg + HalfAz, 0.0f),
		FRotator(CenterElevationDeg - HalfEl, CenterAzimuthDeg + HalfAz, 0.0f),
		FRotator(CenterElevationDeg - HalfEl, CenterAzimuthDeg - HalfAz, 0.0f)
	};

	for (int32 i = 0; i < 4; ++i)
	{
		const FVector Dir = (OwnerQuat * Corners[i].Quaternion()).GetForwardVector();
		DrawDebugLine(World, Origin, Origin + Dir * VisualRange, FrustumColor, false, -1.0f, 0, 1.2f);

		const FVector DirNext = (OwnerQuat * Corners[(i + 1) % 4].Quaternion()).GetForwardVector();
		DrawDebugLine(World, Origin + Dir * VisualRange, Origin + DirNext * VisualRange, FrustumColor, false, -1.0f, 0, 0.8f);
	}

	// 2. Draw elevation bar scan lines across the volume
	const float ElevationStep = (ElevationBars > 1) ? ElevationHeightDeg / static_cast<float>(ElevationBars - 1) : 0.0f;
	for (int32 BarIdx = 0; BarIdx < ElevationBars; ++BarIdx)
	{
		const float BarEl = CenterElevationDeg - HalfEl + (ElevationStep * BarIdx);
		const FVector LeftBarDir = (OwnerQuat * FRotator(BarEl, CenterAzimuthDeg - HalfAz, 0.0f).Quaternion()).GetForwardVector();
		const FVector RightBarDir = (OwnerQuat * FRotator(BarEl, CenterAzimuthDeg + HalfAz, 0.0f).Quaternion()).GetForwardVector();

		const FColor BarColor = (BarIdx == ActiveBar) ? FColor::Yellow : FColor(0, 140, 70);
		const float BarThickness = (BarIdx == ActiveBar) ? 2.0f : 0.6f;
		DrawDebugLine(World, Origin + LeftBarDir * VisualRange, Origin + RightBarDir * VisualRange, BarColor, false, -1.0f, 0, BarThickness);
	}
}

void FAircraftCombatDebug::DrawAntennaBeam(
	const UWorld* World,
	const FVector& Origin,
	const FRotator& Orientation,
	float BeamAzimuthDeg,
	float BeamElevationDeg,
	float BeamAzHalfWidthDeg,
	float BeamElHalfWidthDeg,
	float VisualRange,
	bool bDrawCone,
	const FColor& BeamColor)
{
	if (!World || VisualRange <= 0.0f)
	{
		return;
	}

	const FQuat OwnerQuat = Orientation.Quaternion();
	const FRotator BeamRotator(BeamElevationDeg, BeamAzimuthDeg, 0.0f);
	const FVector BeamDir = (OwnerQuat * BeamRotator.Quaternion()).GetForwardVector();

	// Antenna boresight ray
	DrawDebugLine(World, Origin, Origin + BeamDir * (VisualRange * 1.05f), BeamColor, false, -1.0f, 0, 2.5f);

	if (bDrawCone)
	{
		const float AzConeRad = FMath::DegreesToRadians(FMath::Max(1.0f, BeamAzHalfWidthDeg));
		const float ElConeRad = FMath::DegreesToRadians(FMath::Max(1.0f, BeamElHalfWidthDeg));
		DrawDebugCone(World, Origin, BeamDir, VisualRange * 0.85f, AzConeRad, ElConeRad, 16, BeamColor, false, -1.0f, 0, 1.0f);
	}
}

void FAircraftCombatDebug::DrawTrackSymbology(
	const UWorld* World,
	const FVector& RadarLocation,
	const FRadarTrack& Track,
	bool bDrawVelocityVector)
{
	if (!World || !Track.TrackedActor.IsValid())
	{
		return;
	}

	const AActor* TargetActor = Track.TrackedActor.Get();
	const FVector TargetPos = TargetActor->GetActorLocation();

	FColor TrackColor;
	float LineThickness = 1.0f;
	FString StatusPrefix;

	switch (Track.Status)
	{
		case ERadarTrackStatus::Locked:
			TrackColor = FColor::Red;
			LineThickness = 3.0f;
			StatusPrefix = TEXT("[STT LOCK]");
			break;
		case ERadarTrackStatus::Bugged:
			TrackColor = FColor::Magenta;
			LineThickness = 2.0f;
			StatusPrefix = TEXT("[PDT BUG]");
			break;
		case ERadarTrackStatus::Tracked:
			TrackColor = FColor(0, 255, 128);
			LineThickness = 1.5f;
			StatusPrefix = TEXT("[TWS]");
			break;
		case ERadarTrackStatus::Lost:
			TrackColor = FColor(140, 140, 140);
			LineThickness = 0.8f;
			StatusPrefix = TEXT("[LOST]");
			break;
		case ERadarTrackStatus::Jammed:
			TrackColor = FColor::Yellow;
			LineThickness = 1.5f;
			StatusPrefix = TEXT("[JAMMED]");
			break;
		case ERadarTrackStatus::Search:
		default:
			TrackColor = FColor::White;
			LineThickness = 1.0f;
			StatusPrefix = TEXT("[HIT]");
			break;
	}

	// Line from radar to contact
	DrawDebugLine(World, RadarLocation, TargetPos, TrackColor, false, -1.0f, 0, LineThickness);

	// Diamond / marker
	DrawDebugSphere(World, TargetPos, 80.0f, 8, TrackColor, false, -1.0f, 0, LineThickness);

	if (Track.Status == ERadarTrackStatus::Locked || Track.Status == ERadarTrackStatus::Bugged)
	{
		DrawDebugBox(World, TargetPos, FVector(120.0f), TrackColor, false, -1.0f, 0, LineThickness + 0.5f);
	}

	// Velocity vector arrow
	if (bDrawVelocityVector && Track.EstimatedVelocity.SizeSquared() > 10000.0f)
	{
		const FVector VelArrowEnd = TargetPos + Track.EstimatedVelocity.GetSafeNormal() * 300.0f;
		DrawDebugDirectionalArrow(World, TargetPos, VelArrowEnd, 50.0f, TrackColor, false, -1.0f, 0, 1.2f);
	}

	// Clean, single tag above contact
	DrawDebugString(World, TargetPos + FVector(0, 0, 140.0f),
		FString::Printf(TEXT("%s T%d | %s | %.1fkm %s"),
			*StatusPrefix, Track.TrackID,
			*TargetActor->GetName(),
			Track.Range / 100000.0f,
			Track.bIsNotching ? TEXT("[NOTCH]") : TEXT("")),
		nullptr, TrackColor, -1.0f, true, 1.0f);
}

// 3D RWR visualization

void FAircraftCombatDebug::DrawRWRThreatStrobes(
	const UWorld* World,
	const FVector& AircraftLocation,
	const TArray<FRWRThreatEntry>& Threats,
	bool bDrawThreatStrobes)
{
	if (!World || !bDrawThreatStrobes)
	{
		return;
	}

	for (const FRWRThreatEntry& Threat : Threats)
	{
		if (!Threat.SourceActor.IsValid())
		{
			continue;
		}

		FColor ThreatColor;
		float LineThickness = 1.0f;

		switch (Threat.ThreatType)
		{
			case ERWRThreatType::MissileLaunch:
			case ERWRThreatType::MissileSeeker:
				ThreatColor = FColor::Red;
				LineThickness = 2.5f;
				break;
			case ERWRThreatType::LockOnRadar:
				ThreatColor = FColor::Orange;
				LineThickness = 2.0f;
				break;
			case ERWRThreatType::TrackingRadar:
				ThreatColor = FColor::Yellow;
				LineThickness = 1.5f;
				break;
			case ERWRThreatType::SearchRadar:
				ThreatColor = FColor::Green;
				LineThickness = 1.0f;
				break;
			default:
				ThreatColor = FColor::White;
				break;
		}

		const FVector SourcePos = Threat.SourceActor->GetActorLocation();
		DrawDebugLine(World, AircraftLocation, SourcePos, ThreatColor, false, -1.0f, 0, LineThickness);
		DrawDebugSphere(World, SourcePos, 80.0f, 8, ThreatColor, false, -1.0f, 0, LineThickness);

		const UEnum* TypeEnum = StaticEnum<ERWRThreatType>();
		const FString TypeName = TypeEnum ? TypeEnum->GetDisplayNameTextByValue(static_cast<int64>(Threat.ThreatType)).ToString() : TEXT("?");

		DrawDebugString(World, SourcePos + FVector(0, 0, 160.0f),
			FString::Printf(TEXT("[RWR T%d] %s | Brg: %.0f° | Rng: %.1fkm"),
				Threat.ThreatID, *TypeName, Threat.BearingDegrees, Threat.Range / 100000.0f),
			nullptr, ThreatColor, -1.0f, true, 0.95f);
	}
}

void FAircraftCombatDebug::DrawTargetDesignationBox(
	const UWorld* World,
	const FVector& Location,
	const FRotator& Rotation,
	const FColor& Color,
	float BoxSize)
{
	if (!World)
	{
		return;
	}

	DrawDebugBox(World, Location, FVector(BoxSize), Rotation.Quaternion(), Color, false, -1.0f, 0, 2.0f);

	const float CrosshairExtent = BoxSize * 1.5f;
	DrawDebugLine(World, Location - FVector(CrosshairExtent, 0, 0), Location + FVector(CrosshairExtent, 0, 0), Color, false, -1.0f, 0, 1.5f);
	DrawDebugLine(World, Location - FVector(0, CrosshairExtent, 0), Location + FVector(0, CrosshairExtent, 0), Color, false, -1.0f, 0, 1.5f);
	DrawDebugLine(World, Location - FVector(0, 0, CrosshairExtent), Location + FVector(0, 0, CrosshairExtent), Color, false, -1.0f, 0, 1.5f);
}

// 3D missile & seeker visualization

void FAircraftCombatDebug::DrawMissileSeekerCone(
	const UWorld* World,
	const FVector& SeekerLocation,
	const FVector& SeekerForward,
	float ConeHalfAngleDeg,
	float VisualRange,
	const FColor& ConeColor)
{
	if (!World || VisualRange <= 0.0f)
	{
		return;
	}

	const float ConeAngleRad = FMath::DegreesToRadians(FMath::Max(1.0f, ConeHalfAngleDeg));
	DrawDebugCone(World, SeekerLocation, SeekerForward, VisualRange, ConeAngleRad, ConeAngleRad, 20, ConeColor, false, -1.0f, 0, 1.0f);
}

void FAircraftCombatDebug::DrawMissileGuidanceVector(
	const UWorld* World,
	const FVector& MissileLocation,
	const FVector& VelocityVector,
	const FVector& CommandedAccelVector,
	const AActor* TargetActor)
{
	if (!World)
	{
		return;
	}

	// Velocity vector
	if (VelocityVector.SizeSquared() > 10000.0f)
	{
		const FVector VelEnd = MissileLocation + VelocityVector.GetSafeNormal() * 400.0f;
		DrawDebugDirectionalArrow(World, MissileLocation, VelEnd, 60.0f, FColor::Cyan, false, -1.0f, 0, 1.5f);
	}

	// Commanded acceleration steering vector
	if (CommandedAccelVector.SizeSquared() > 1000.0f)
	{
		const FVector AccelEnd = MissileLocation + CommandedAccelVector.GetSafeNormal() * 250.0f;
		DrawDebugDirectionalArrow(World, MissileLocation, AccelEnd, 40.0f, FColor::Purple, false, -1.0f, 0, 1.5f);
	}

	// Line of sight to target
	if (IsValid(TargetActor))
	{
		DrawDebugLine(World, MissileLocation, TargetActor->GetActorLocation(), FColor::Red, false, -1.0f, 0, 1.2f);
	}
}

void FAircraftCombatDebug::DrawDatalinkLine(
	const UWorld* World,
	const FVector& TransmitterLocation,
	const FVector& ReceiverLocation,
	const FColor& LinkColor)
{
	if (!World)
	{
		return;
	}

	DrawDebugLine(World, TransmitterLocation, ReceiverLocation, LinkColor, false, -1.0f, 0, 1.0f);
}

// 3D cannon & ballistic tracers

void FAircraftCombatDebug::DrawBulletTracer(
	const UWorld* World,
	const FVector& StartPos,
	const FVector& EndPos,
	float DeltaTime,
	const FColor& TracerColor)
{
	if (!World)
	{
		return;
	}

	// Draw for exact frame duration (FMath::Max(DeltaTime, 0.02f)) to prevent ghost lines in the air
	DrawDebugLine(World, StartPos, EndPos, TracerColor, false, FMath::Max(DeltaTime, 0.02f), 0, 1.5f);
}

void FAircraftCombatDebug::DrawBulletImpact(
	const UWorld* World,
	const FVector& ImpactPoint,
	const FVector& ImpactNormal,
	float Lifetime,
	const FColor& ImpactColor)
{
	if (!World)
	{
		return;
	}

	DrawDebugPoint(World, ImpactPoint, 10.0f, ImpactColor, false, Lifetime);
	DrawDebugLine(World, ImpactPoint, ImpactPoint + (ImpactNormal * 40.0f), FColor::Green, false, Lifetime, 0, 1.5f);
}
