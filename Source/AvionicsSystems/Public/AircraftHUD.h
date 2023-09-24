// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "AircraftDisplay.h"
#include "AircraftHUD.generated.h"

class APawn;
class UCameraComponent;
class USceneComponent;

/**
 * Configuration structure for Ballistic Stadiametric Gun Funnel (EEGS)
 */
USTRUCT(BlueprintType)
struct FGunFunnelConfig
{
	GENERATED_BODY()

	/** Toggles whether the funnel calculation and rendering are active (e.g., enable in Dogfight/A-A Gun mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State & Control")
	bool bIsEnabled = true;

	/** Enables the dynamic historical sweep bending of the funnel during high-G maneuvers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State & Control")
	bool bEnableKinematicSweep = true;

	/** Multiplier for maneuver sweep bending intensity (0 = rigid lead line, 1 = realistic historical stream, >1 = exaggerated) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State & Control", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float SweepMultiplier = 1.0f;

	/** Interpolation speed for smoothing physics angular velocity to eliminate HUD jitter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State & Control")
	float AngularInterpSpeed = 15.0f;

	/** Minimum dot product against camera forward to cull points outside the forward field of view (prevents peripheral/behind-camera artifacts) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State & Control", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CameraCullDotThreshold = 0.0f;

	/** Target wingspan in centimeters used to scale the funnel width (1000 cm represents a standard 10-meter fighter target) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadiametric Target Settings")
	float TargetWingspanCM = 1000.0f;

	/** Bullet muzzle velocity in cm/s (1036 m/s for the M61A1 20mm cannon) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics & Sampling")
	float MuzzleSpeedCMS = 103600.0f;

	/** Number of trajectory slices sampled along the funnel length */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics & Sampling", meta = (ClampMin = "4", ClampMax = "32", UIMin = "4", UIMax = "32"))
	int32 SampleCount = 16;

	/** Nearest time-of-flight sample in seconds (represents closest combat range, ~600 ft / 180 m) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics & Sampling")
	float MinTOF = 0.15f;

	/** Furthest time-of-flight sample in seconds (represents max gun range, ~3000 ft / 900 m) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics & Sampling")
	float MaxTOF = 1.4f;

	/** HUD phosphor green tint for the funnel lines */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals & Rendering")
	FLinearColor Color = FLinearColor(0.0f, 1.0f, 0.2f, 1.0f);

	/** Thickness of the drawn rail lines in Slate units */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals & Rendering")
	float LineThickness = 6.0f;

	/** Enables anti-aliasing on the Slate line draw calls for smooth geometry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals & Rendering")
	bool bAntialias = true;
};

// Typedef alias for backward-compatibility if referenced as FEEGSFunnel
using FEEGSFunnel = FGunFunnelConfig;

/**
 * Aircraft HUD UserWidget class for AvionicsSystems
 */
UCLASS()
class AVIONICSSYSTEMS_API UAircraftHUD : public UAircraftDisplay
{
	GENERATED_BODY()

public:

	/** 2D scale factor applied to the projected HUD coordinates */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Up Display")
	FVector2D ProjectionScale = FVector2D(-12.0f, -12.0f);

	/** Socket name on the aircraft mesh to locate the cannon's physical position and forward vector */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Up Display|Gun Properties")
	FName MuzzleSocketName = FName(TEXT("Muzzle"));

	/** Ballistic stadiametric gun funnel configuration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Up Display|Gun Properties")
	FGunFunnelConfig FunnelConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Up Display|Gun Properties")
	float BulletsSpeed = 105000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Up Display|Gun Properties|Firing Solution")
	float MinFiringRange = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Up Display|Gun Properties|Firing Solution")
	float MaxFiringRange = 121920.0f;

	/** Optional name or tag of the cockpit camera component on the aircraft pawn. If None, falls back to the first camera found */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Up Display|Camera", meta = (DisplayName = "Camera Component Name"))
	FName CameraComponentName = NAME_None;

	/** Projects a 3D world location onto the HUD widget 2D space */
	UFUNCTION(BlueprintCallable, Category = "Aircraft HUD") 
	bool ProjectLocationToHUD(const FVector& Location, FVector2D& Result) const;

	/** Calculates gun funnel rail points in HUD canvas space using 3D collimated projection */
	UFUNCTION(BlueprintCallable, Category = "Aircraft HUD|Gunsight")
	void CalculateGunFunnel(const FGeometry& MyGeometry);

	/** Calculates the 3D world location at optical infinity for the Gun Boresight Cross */
	UFUNCTION(BlueprintPure, Category = "Aircraft HUD|Gunsight")
	bool CalculateBoresightCrossLocation(FVector& Result) const;

	/** Calculates the 3D director gunsight lead pipper world position */
	UFUNCTION(BlueprintPure, Category = "Aircraft HUD|Gunsight|Firing Solution")
	bool FindFiringSolution(FVector& Result) const;

	/** Calculates the target closure rate */
	UFUNCTION(BlueprintPure, Category = "Aircraft HUD|Gunsight|Firing Solution")
	float GetTargetClosureRate() const;

	/** Calculates the 3D world location at optical infinity for the Flight Path Marker */
	UFUNCTION(BlueprintPure, Category = "Aircraft HUD")
	bool CalculateFlightPathPosition(FVector& Result) const;

	/** Calculates the 3D world location at optical infinity for the Horizon Line */
	UFUNCTION(BlueprintPure, Category = "Aircraft HUD")
	bool GetHorizonPosition(FVector& Result) const;

	/** Returns the player camera component */
	UFUNCTION(BlueprintPure, Category = "Aircraft HUD")
	FORCEINLINE UCameraComponent* GetPlayerCamera() const { return PlayerCamera; }

	/** Returns the cached 2D canvas points along the center spine of the gun funnel */
	UFUNCTION(BlueprintPure, Category = "Aircraft HUD|Gunsight")
	FORCEINLINE TArray<FVector2D> GetCachedFunnelPoints() const { return CachedFunnelPoints; }

	/** Returns the cached 2D canvas points along the left rail of the gun funnel */
	UFUNCTION(BlueprintPure, Category = "Aircraft HUD|Gunsight")
	FORCEINLINE TArray<FVector2D> GetCachedLeftRailPoints() const { return CachedLeftRailPoints; }

	/** Returns the cached 2D canvas points along the right rail of the gun funnel */
	UFUNCTION(BlueprintPure, Category = "Aircraft HUD|Gunsight")
	FORCEINLINE TArray<FVector2D> GetCachedRightRailPoints() const { return CachedRightRailPoints; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeInitialization() override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled
	) const override;

private:
	/** Pre-allocated cached line buffer for gun funnel center spine */
	TArray<FVector2D> CachedFunnelPoints;

	/** Pre-allocated cached line buffer for left gun funnel rail */
	TArray<FVector2D> CachedLeftRailPoints;

	/** Pre-allocated cached line buffer for right gun funnel rail */
	TArray<FVector2D> CachedRightRailPoints;

	/** Cached scene component containing the muzzle socket to avoid per-frame lookups */
	mutable TWeakObjectPtr<USceneComponent> CachedMuzzleComponent;

	/** Low-pass filtered angular velocity to eliminate physics jitter */
	FVector FilteredAngularVelocity = FVector::ZeroVector;

	/** Player camera component reference */
	UPROPERTY()
	TObjectPtr<UCameraComponent> PlayerCamera;
};
