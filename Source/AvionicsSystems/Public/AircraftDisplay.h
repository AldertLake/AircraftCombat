// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "RadarDisplayGeometry.h"
#include "AircraftDisplay.generated.h"

class AActor;
class APawn;
class UWidget;
class UAircraftDisplayComponent;
class UModularMissionManagement;
class UAircraftRadarComponent;
class URadarWarningReceiverComponent;

/**
 * Base UserWidget class for aircraft avionics and cockpit displays
 */
UCLASS()
class AVIONICSSYSTEMS_API UAircraftDisplay : public UUserWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration|Collision Detection")
	float BreakXTimeThreshold = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration|Collision Detection")
	float BreakXTraceDistance = 1000000.0f;


	/** Enables independent mode where the display operates without requiring a UAircraftDisplayComponent (e.g. standalone 2D screen HUDs) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration", meta = (ExposeOnSpawn = "true", DisplayName = "Independent Mode"))
	bool bIndependentMode = false;

	/** Returns the locked target actor */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE AActor* GetLockedTarget() const { return LockedTarget; }

	/** Returns the player aircraft pawn */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE APawn* GetPlayerAircraft() const { return PlayerAircraft; }

	/** Returns the aircraft display component (returns nullptr if in Independent Mode) */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE UAircraftDisplayComponent* GetDisplayComponent() const { return bIndependentMode ? nullptr : DisplayComponent; }

	/** Returns the modular mission management component */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display|Mission Management")
	FORCEINLINE UModularMissionManagement* GetModularMissionManagement() const { return ModularMissionManagement; }

	/** Returns the aircraft radar component */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display|Radar", meta = (DisplayName = "Get Aircraft Radar"))
	FORCEINLINE UAircraftRadarComponent* GetAircraftRadar() const { return AircraftRadar; }

	/** Returns the radar warning receiver component */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display|RWR", meta = (DisplayName = "Get Radar Warning Receiver"))
	FORCEINLINE URadarWarningReceiverComponent* GetRadarWarningReceiver() const { return RadarWarningReceiver; }

	/** Initializes the display with essential references (Internal C++) */
	void InitializeDisplay(APawn* InPlayerAircraft, UAircraftDisplayComponent* InDisplayComponent);

	/** Event called when the display has been initialized */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aircraft Display", meta = (DisplayName = "On Display Initialized"))
	void OnDisplayInitialized();

	/** Called when 2D stick/TDC axis input is forwarded to this display while it is the active Sensor of Interest (SOI) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aircraft Display|Input", meta = (DisplayName = "Forward Axis Input"))
	void ForwardAxisInput(FVector2D PositionXY);

	/** Called when a button/trigger action input is forwarded to this display while it is the active Sensor of Interest (SOI) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aircraft Display|Input", meta = (DisplayName = "Forward Action Input"))
	void ForwardActionInput(FKey Key, bool bPressed);

	/** Called when this display gains or loses Sensor of Interest (SOI) focus */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aircraft Display|SOI", meta = (DisplayName = "SOI State Changed"))
	void OnSOIStateChanged(bool bNewState);

	/** Sets Sensor of Interest (SOI) status on this display and its parent component */
	UFUNCTION(BlueprintCallable, Category = "Aircraft Display|SOI", meta = (DisplayName = "Set Sensor Of Interest"))
	void SetSensorOfInterest(bool bEnable);

	/** Returns true if this display is currently the active Sensor of Interest (SOI) */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display|SOI", meta = (DisplayName = "Is Sensor Of Interest"))
	bool IsSensorOfInterest() const;

	/** Internal notification from the display component when SOI state changes */
	void NotifySOIStateChanged(bool bNewState);

	/** Returns true if the display has been initialized with valid references */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display", meta = (DisplayName = "Is Display Initialized"))
	FORCEINLINE bool IsDisplayInitialized() const { return bIsDisplayInitialized; }

	/** Assigns the locked target reference */
	UFUNCTION(BlueprintCallable, Category = "Aircraft Display")
	void AssignLockedTarget(AActor* InLockedTarget);

	/** Checks if a symbol widget's render translation is within the specified boundary box */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display", meta = (DisplayName = "Is Symbol In Display Boundary"))
	bool IsSymbolInDisplayBoundarie(UWidget* SymbolWidget, FVector2D XLimits = FVector2D(-100.0f, 100.0f), FVector2D YLimits = FVector2D(-100.0f, 100.0f)) const;

	/** Verifies if a UWidget is within display boundaries (Deprecated: Use IsSymbolInDisplayBoundarie instead) */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display", meta = (DeprecatedFunction, DeprecationMessage = "Use IsSymbolInDisplayBoundarie instead."))
	bool IsSymbolInHUD(UWidget* SymbolWidget) const;

	/** Evaluates altitude, sink rate, and terrain to trigger the ground collision warning */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display|Flight Safety")
	bool CheckGroundCollision() const;

	/** Projects a world point into the chosen radar display. The result is not clamped; false means outside the display. */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display|UI Geometry", meta = (DisplayName = "Get World Widget Position"))
	static bool GetWorldWidgetPosition(const FVector& TargetLocation, const FRadarDisplayProjection& Projection,
		FVector2D& OutWidgetPosition);


protected:
	virtual void NativeConstruct() override;

	/** Native C++ initialization event called when the display has valid references */
	virtual void NativeInitialization();

	/** Modular mission management component reference */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft Display|Mission Management", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<UModularMissionManagement> ModularMissionManagement;

	/** Airborne radar component reference */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft Display|Radar", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<UAircraftRadarComponent> AircraftRadar;

	/** Radar Warning Receiver component reference */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft Display|RWR", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<URadarWarningReceiverComponent> RadarWarningReceiver;

	/** Player aircraft pawn reference */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft Display", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<APawn> PlayerAircraft;

	/** Locked target actor reference */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Display")
	TObjectPtr<AActor> LockedTarget;

	/** Aircraft display component reference */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Display")
	TObjectPtr<UAircraftDisplayComponent> DisplayComponent;

	/** Internal display initialization state flag */
	bool bIsDisplayInitialized = false;

	/** Local Sensor of Interest state flag (used as fallback when operating in Independent Mode) */
	bool bLocalSensorOfInterest = false;
};
