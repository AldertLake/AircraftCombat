// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AircraftDataLinkSubsystem.generated.h"

class UAircraftRadarComponent;

/** Server-only scheduler and radio topology for independent tactical data-link participants. */
UCLASS()
class WEAPONSYSTEMS_API UAircraftDataLinkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;
	void RegisterRadar(UAircraftRadarComponent* Radar);
	void UnregisterRadar(UAircraftRadarComponent* Radar);

	/** Time between scheduled transmission windows. */
	UPROPERTY(EditAnywhere, Category = "Combat|Data Link", meta = (ClampMin = "0.1"))
	float TransmissionInterval = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Combat|Data Link", meta = (ClampMin = "1"))
	int32 ReportsPerWindow = 16;
	UPROPERTY(EditAnywhere, Category = "Combat|Data Link", meta = (ClampMin = "0"))
	int32 MaxRelayHops = 4;

private:
	TArray<TWeakObjectPtr<UAircraftRadarComponent>> Participants;
	TMap<int32, int32> NextReportIndex;
	int32 NextParticipantID = 1;
	float TransmissionAccumulator = 0.0f;
	bool HasPhysicalPath(const UAircraftRadarComponent* First, const UAircraftRadarComponent* Second,
		const FVector& From, const FVector& To) const;
	void TransmitWindow(float WorldTime);
};
