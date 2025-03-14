// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AircraftCombatCommonTypes.h"
#include "AircraftCombatSettings.generated.h"

/**
 * Global Developer Settings for Aircraft Combat and Avionics.
 * Configures universal vehicle domain identification tags and RWR threat priority.
 * Accessible in Unreal Editor via Project Settings -> Plugins -> Aircraft Combat.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Aircraft Combat"))
class WEAPONSYSTEMS_API UAircraftCombatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAircraftCombatSettings();

	/** Gets the default settings instance */
	static const UAircraftCombatSettings* Get();

	//~ Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Aircraft Combat"); }
#if WITH_EDITOR
	virtual FText GetSectionText() const override { return NSLOCTEXT("AircraftCombat", "AircraftCombatSettingsSection", "Aircraft Combat"); }
	virtual FText GetSectionDescription() const override { return NSLOCTEXT("AircraftCombat", "AircraftCombatSettingsDescription", "Universal settings for aircraft combat, vehicle classification tags, and RWR threat priority."); }
#endif
	//~ End UDeveloperSettings Interface

	// ========================================================================
	// Universal Vehicle Classification Tags
	// ========================================================================

	/** Tags that identify airborne vehicles / aircraft (default domain for untagged pawns) */
	UPROPERTY(EditAnywhere, config, Category = "Vehicle Classification", meta = (DisplayName = "Air Identification Tags"))
	TArray<FName> AirIdentificationTags;

	/** Tags that identify ground vehicles, armor, artillery, and structures */
	UPROPERTY(EditAnywhere, config, Category = "Vehicle Classification", meta = (DisplayName = "Ground Identification Tags"))
	TArray<FName> GroundIdentificationTags;

	/** Tags that identify maritime / naval surface vessels */
	UPROPERTY(EditAnywhere, config, Category = "Vehicle Classification", meta = (DisplayName = "Sea Identification Tags"))
	TArray<FName> SeaIdentificationTags;

	/** Tags that identify guided missile actors */
	UPROPERTY(EditAnywhere, config, Category = "Vehicle Classification", meta = (DisplayName = "Missile Identification Tags"))
	TArray<FName> MissileIdentificationTags;

	/**
	 * Resolves the operational domain for a given candidate actor by inspecting its Actor Tags and Component Tags.
	 * Evaluates Missile -> Sea -> Ground -> Air, defaulting to Air if untagged.
	 *
	 * @param Candidate The actor to inspect
	 * @param bSearchComponentTags If true, also inspects ComponentTags on the candidate's components
	 * @return Resolved ERadarTargetDomain (Air, Ground, Sea, Missile)
	 */
	UFUNCTION(BlueprintPure, Category = "Aircraft Combat|Classification")
	ERadarTargetDomain ResolveTargetDomain(const AActor* Candidate, bool bSearchComponentTags = true) const;

	// ========================================================================
	// RWR Priority & Diamond Threat Settings
	// ========================================================================

	/** Threat type priority ranking (index 0 = highest danger) for selecting the RWR Diamond threat. */
	UPROPERTY(EditAnywhere, config, Category = "RWR Priority", meta = (DisplayName = "Threat Type Priority (Index 0 = Highest Danger)"))
	TArray<ERWRThreatType> ThreatTypePriority;

	/** Vehicle domain priority ranking (index 0 = highest danger) used as tie-breaker for RWR Diamond threat. */
	UPROPERTY(EditAnywhere, config, Category = "RWR Priority", meta = (DisplayName = "Threat Vehicle Type Priority (Index 0 = Highest Danger)"))
	TArray<ERadarTargetDomain> ThreatVehicleTypePriority;
};
