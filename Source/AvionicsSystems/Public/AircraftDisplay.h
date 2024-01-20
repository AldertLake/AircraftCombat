// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AircraftDisplay.generated.h"

class AActor;
class AAircraftPawn;
class UWidget;
class UAircraftDisplayComponent;
class UModularMissionManagement;

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

	/** Enables automatic search and binding of the Modular Mission Management component on the player aircraft */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration|Mission Management", meta = (DisplayName = "Support Mission Management"))
	bool bSupportMissionManagement = false;

	/** Enables independent mode where the display operates without requiring a UAircraftDisplayComponent (e.g. standalone 2D screen HUDs) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration", meta = (ExposeOnSpawn = "true", DisplayName = "Independent Mode"))
	bool bIndependentMode = false;

	/** Returns the locked target actor */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE AActor* GetLockedTarget() const { return LockedTarget; }

	/** Returns the player aircraft pawn */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE AAircraftPawn* GetPlayerAircraft() const { return PlayerAircraft; }

	/** Returns the aircraft display component (returns nullptr if in Independent Mode) */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE UAircraftDisplayComponent* GetDisplayComponent() const { return bIndependentMode ? nullptr : DisplayComponent; }

	/** Returns the modular mission management component */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display|Mission Management")
	FORCEINLINE UModularMissionManagement* GetModularMissionManagement() const { return ModularMissionManagement; }

	/** Initializes the display with essential references (Internal C++) */
	void InitializeDisplay(AAircraftPawn* InPlayerAircraft, UAircraftDisplayComponent* InDisplayComponent);

	/** Event called when the display has been initialized */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aircraft Display", meta = (DisplayName = "On Display Initialized"))
	void OnDisplayInitialized();

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

protected:
	virtual void NativeConstruct() override;

	/** Native C++ initialization event called when the display has valid references */
	virtual void NativeInitialization();

	/** Modular mission management component reference */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft Display|Mission Management", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<UModularMissionManagement> ModularMissionManagement;

	/** Player aircraft pawn reference */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft Display", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<AAircraftPawn> PlayerAircraft;

	/** Locked target actor reference */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Display")
	TObjectPtr<AActor> LockedTarget;

	/** Aircraft display component reference */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Display")
	TObjectPtr<UAircraftDisplayComponent> DisplayComponent;

	/** Internal display initialization state flag */
	bool bIsDisplayInitialized = false;
};
