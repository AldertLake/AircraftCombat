// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
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
	/** Radar diagnostics refresh at 10 Hz; each sample remains visible until the next one. */
	static constexpr float RadarDebugRefreshSeconds = 0.1f;
	static constexpr float RadarDebugDrawLifetimeSeconds = 0.15f;

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
	static void PrintRadarTelemetry(const UObject* RadarInstance, int32 LineOffset, const FString& Text,
		const FColor& Color = FColor::Cyan, float TimeToDisplay = RadarDebugDrawLifetimeSeconds);
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
		const FColor& FrustumColor = FColor(0, 220, 100),
		bool bCellCenteredBars = false
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

	/** Priority levels for actor 3D debug text (higher priority replaces lower, enforcing One-Text Rule) */
	enum class EActorDebugTextPriority : uint8
	{
		None = 0,
		Lost = 1,          // [LOST] (Grey)
		SearchHit = 2,     // [HIT] (White)
		Jammed = 3,        // [JAMMED] (Yellow)
		Tracked = 4,       // [TWS] (Green)
		Bugged = 5,        // [PDT BUG] (Magenta)
		RWRThreat = 6,     // [RWR] (Yellow / Orange)
		Locked = 7,        // [STT LOCK] (Red)
		MissileThreat = 8  // [MISSILE WARNING] (Flashing Red)
	};

	/**
	 * Draws a prioritized 3D debug string attached to an actor.
	 * Keeps one attached label per actor across radar and RWR refresh rates.
	 * A higher-priority source takes ownership until it stops refreshing.
	 */
	static void DrawPrioritizedActorDebugText(
		const UWorld* World,
		const UObject* DebugSource,
		AActor* TargetActor,
		EActorDebugTextPriority Priority,
		const FString& Text,
		const FColor& Color,
		const FVector& LocalOffset = FVector(0, 0, 140.0f),
		float FontScale = 1.0f
	);

	/** Flushes/resets all active actor debug text cache */
	static void ResetActorDebugTextCache();

#if WITH_DEV_AUTOMATION_TESTS
	static bool GetActorDebugTextSelectionForTest(const AActor* TargetActor,
		const UObject*& OutSource, EActorDebugTextPriority& OutPriority);
#endif

	static void DrawTrackSymbology(
		const UWorld* World,
		const UObject* DebugSource,
		const FVector& RadarLocation,
		const FRadarTrack& Track,
		bool bDrawVelocityVector = true
	);

	// 3D RWR visualization
	static void DrawRWRThreatStrobes(
		const UWorld* World,
		const UObject* DebugSource,
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
