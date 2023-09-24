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
class APawn;
class UWidget;
class UAircraftDisplayComponent;

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

	/** Returns the locked target actor */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE AActor* GetLockedTarget() const { return LockedTarget; }

	/** Returns the player aircraft pawn */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE APawn* GetPlayerAircraft() const { return PlayerAircraft; }

	/** Returns the aircraft display component */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE UAircraftDisplayComponent* GetDisplayComponent() const { return DisplayComponent; }

	/** Initializes the display with essential references (Internal C++) */
	void InitializeDisplay(APawn* InPlayerAircraft, UAircraftDisplayComponent* InDisplayComponent);

	/** Event called when the display has been initialized */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aircraft Display", meta = (DisplayName = "On Display Initialized"))
	void OnDisplayInitialized();

	/** Returns true if the display has been initialized with valid references */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display", meta = (DisplayName = "Is Display Initialized"))
	FORCEINLINE bool IsDisplayInitialized() const { return bIsDisplayInitialized; }

	/** Assigns the locked target reference */
	UFUNCTION(BlueprintCallable, Category = "Aircraft Display")
	void AssignLockedTarget(AActor* InLockedTarget);

	/** Verifies if a UWidget is currently within the screen viewport bounds */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	bool IsSymbolInHUD(UWidget* SymbolWidget) const;

	/** Evaluates altitude, sink rate, and terrain to trigger the ground collision warning */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display|Flight Safety")
	bool CheckGroundCollision() const;

protected:
	/** Native C++ initialization event called when the display has valid references */
	virtual void NativeInitialization();

	/** Player aircraft pawn reference */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Display")
	TObjectPtr<APawn> PlayerAircraft;

	/** Locked target actor reference */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Display")
	TObjectPtr<AActor> LockedTarget;

	/** Aircraft display component reference */
	UPROPERTY(BlueprintReadOnly, Category = "Aircraft Display")
	TObjectPtr<UAircraftDisplayComponent> DisplayComponent;

	/** Internal display initialization state flag */
	bool bIsDisplayInitialized = false;
};
