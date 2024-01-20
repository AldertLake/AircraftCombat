// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

class UWorld;
class AActor;
struct FRadarTrack;
struct FRWRThreatEntry;

/**
 * Static debug and telemetry utility for AircraftCombat plugin.
 *
 * Provides a unified, coordinated visualization system with dedicated
 * non-colliding on-screen message key partitions, eliminating screen
 * flickering, overlapping 3D strings, and arbitrary persistent debug lines.
 */
class WEAPONSYSTEMS_API FAircraftCombatDebug
{
public:
	// Telemetry key partitions (ensures non-colliding message IDs)
	static constexpr int32 KeyBase_Radar   = 2100; // 2100 - 2119
	static constexpr int32 KeyBase_RWR     = 2120; // 2120 - 2139
	static constexpr int32 KeyBase_Missile = 2140; // 2140 - 2159
	static constexpr int32 KeyBase_SMS     = 2160; // 2160 - 2179

	static constexpr int32 Key_RadarBase   = KeyBase_Radar;
	static constexpr int32 Key_RWRBase     = KeyBase_RWR;
	static constexpr int32 Key_MissileBase = KeyBase_Missile;
	static constexpr int32 Key_SMSBase     = KeyBase_SMS;

	// On-screen HUD telemetry helpers
	static void PrintRadarTelemetry(int32 LineOffset, const FString& Text, const FColor& Color = FColor::Cyan, float TimeToDisplay = 0.0f);
	static void PrintRWRTelemetry(int32 LineOffset, const FString& Text, const FColor& Color = FColor::Yellow, float TimeToDisplay = 0.0f);
	static void PrintRWRTelemetry(int32 ThreatCount, bool bHasLockOn, bool bHasMissileLaunch);
	static void PrintMissileTelemetry(int32 LineOffset, const FString& Text, const FColor& Color = FColor::Magenta, float TimeToDisplay = 0.0f);
	static void PrintMissileTelemetry(const FString& MissileName, const FString& PhaseName, float ClosureRate, float RangeMeters, bool bSeekerActive, bool bDatalinkActive);
	static void PrintSMSTelemetry(int32 LineOffset, const FString& Text, const FColor& Color = FColor::White, float TimeToDisplay = 0.0f);

	// 3D radar visualization
	static void DrawRadarFrustum(
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
		const FColor& FrustumColor = FColor(0, 220, 100)
	);

	static void DrawAntennaBeam(
		const UWorld* World,
		const FVector& Origin,
		const FRotator& Orientation,
		float BeamAzimuthDeg,
		float BeamElevationDeg,
		float BeamAzHalfWidthDeg,
		float BeamElHalfWidthDeg,
		float VisualRange,
		bool bDrawCone = true,
		const FColor& BeamColor = FColor(255, 220, 40)
	);

	static void DrawTrackSymbology(
		const UWorld* World,
		const FVector& RadarLocation,
		const FRadarTrack& Track,
		bool bDrawVelocityVector = true
	);

	// 3D RWR visualization
	static void DrawRWRThreatStrobes(
		const UWorld* World,
		const FVector& AircraftLocation,
		const TArray<FRWRThreatEntry>& Threats,
		bool bDrawThreatStrobes = true
	);

	/** Draws a 3D designation box and crosshair over a locked or designated target */
	static void DrawTargetDesignationBox(
		const UWorld* World,
		const FVector& Location,
		const FRotator& Rotation,
		const FColor& Color = FColor::Red,
		float BoxSize = 80.0f
	);

	// 3D missile & seeker visualization
	static void DrawMissileSeekerCone(
		const UWorld* World,
		const FVector& SeekerLocation,
		const FVector& SeekerForward,
		float ConeHalfAngleDeg,
		float VisualRange,
		const FColor& ConeColor = FColor::Cyan
	);

	static void DrawMissileGuidanceVector(
		const UWorld* World,
		const FVector& MissileLocation,
		const FVector& VelocityVector,
		const FVector& CommandedAccelVector,
		const AActor* TargetActor = nullptr
	);

	static void DrawDatalinkLine(
		const UWorld* World,
		const FVector& TransmitterLocation,
		const FVector& ReceiverLocation,
		const FColor& LinkColor = FColor(0, 140, 255)
	);

	// 3D cannon & ballistic tracers
	static void DrawBulletTracer(
		const UWorld* World,
		const FVector& StartPos,
		const FVector& EndPos,
		float DeltaTime,
		const FColor& TracerColor = FColor(255, 200, 50)
	);

	static void DrawBulletImpact(
		const UWorld* World,
		const FVector& ImpactPoint,
		const FVector& ImpactNormal,
		float Lifetime = 0.5f,
		const FColor& ImpactColor = FColor::Orange
	);
};
