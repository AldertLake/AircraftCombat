// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "DisplayComponent.generated.h"

class UAircraftDisplay;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;

/**
 * 3D Widget component class for aircraft displays in the cockpit
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class AVIONICSSYSTEMS_API UAircraftDisplayComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UAircraftDisplayComponent();

	/** Whether this component projects its display onto an external mesh component (e.g. cockpit MFD/HUD glass). If false, uses the built-in widget component surface */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration|Display Mesh", meta = (DisplayName = "Has External Display Mesh"))
	bool bHasExternalDisplayMesh = true;

	/** Primitive mesh component reference selectable from pawn components in the details panel before play */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration|Display Mesh", meta = (DisplayName = "Display Mesh", UseComponentPicker, AllowedClasses = "/Script/Engine.PrimitiveComponent", EditCondition = "bHasExternalDisplayMesh", EditConditionHides))
	FComponentReference DisplayMeshReference;

	/** Material slot index on the display mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration|Display Mesh", meta = (DisplayName = "Slot Index", ClampMin = "0", EditCondition = "bHasExternalDisplayMesh", EditConditionHides))
	int32 SlotIndex = 0;

	/** Material applied to the display surface */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration|Display Material", meta = (DisplayName = "Display Material", EditCondition = "bHasExternalDisplayMesh", EditConditionHides))
	TObjectPtr<UMaterialInterface> DisplayMaterial;

	/** Texture parameter name in the display material to bind the render target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Configuration|Display Material", meta = (DisplayName = "Display Parameter", EditCondition = "bHasExternalDisplayMesh", EditConditionHides))
	FName DisplayParameter = FName(TEXT("Display"));

	/** Returns whether this component projects onto an external display mesh */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE bool HasExternalDisplayMesh() const { return bHasExternalDisplayMesh; }

	/** Returns the target display mesh primitive component */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	UPrimitiveComponent* GetDisplayMesh() const;

	/** Returns the dynamic material instance created on the display mesh */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE UMaterialInstanceDynamic* GetDisplayDynamicMaterial() const { return DisplayDynamicMaterial; }

	/** Returns the aircraft display user widget instance created by this component */
	UFUNCTION(BlueprintPure, Category = "Aircraft Display")
	FORCEINLINE UAircraftDisplay* GetAircraftDisplayWidget() const { return AircraftDisplayWidget; }

protected:
	virtual void BeginPlay() override;

	/** Initializes and applies the dynamic material to the target display mesh */
	void InitializeDisplayMaterial();

private:
	/** Aircraft display user widget instance */
	UPROPERTY(Transient)
	TObjectPtr<UAircraftDisplay> AircraftDisplayWidget;

	/** Dynamic material instance created on the display mesh */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayDynamicMaterial;

	/** Cached primitive component pointer for the display mesh */
	UPROPERTY(Transient)
	mutable TObjectPtr<UPrimitiveComponent> DisplayMesh;

	/** Timer handle for delayed BeginPlay material initialization */
	FTimerHandle DisplayMaterialTimerHandle;
};
