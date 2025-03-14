// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "MissileGuidanceComponent.h"
#include "RadarMissileGuidanceComponent.generated.h"

class UAircraftRadarComponent;
struct FRadarLaunchedMissileStatus;

UENUM(BlueprintType)
enum class ERadarMissileFlightPhase : uint8
{
	PreLaunch,
	MidCourse,
	SemiActive,
	Coasting,
	TerminalSearch,
	TerminalTrack,
	SeaSkim,
	Unguided
};

UENUM(BlueprintType)
enum class EActiveSeekerActivation : uint8
{
	Immediate,
	AtRange
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnActiveSeekerActivatedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRadarMissileLockLostSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDataLinkTimeoutSignature, float, TimeSinceLastUpdate);

/** Shared radar seeker, measured target, datalink, and illumination machinery. */
UCLASS(Abstract, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API URadarMissileGuidanceComponent : public UMissileGuidanceComponent
{
	GENERATED_BODY()

public:
	URadarMissileGuidanceComponent();

	UPROPERTY(BlueprintAssignable, Category="Missile|Radar|Events")
	FOnActiveSeekerActivatedSignature OnActiveSeekerActivated;
	UPROPERTY(BlueprintAssignable, Category="Missile|Radar|Events")
	FOnRadarMissileLockLostSignature OnRadarLockLost;
	UPROPERTY(BlueprintAssignable, Category="Missile|Radar|Events")
	FOnDataLinkTimeoutSignature OnDataLinkTimeout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Seeker", meta=(ClampMin="100.0"))
	float ActiveSeekerMaxRange = 2000000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Seeker", meta=(ClampMin="0.0"))
	float ActiveSeekerRange = 1600000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Seeker", meta=(ClampMin="1.0", ClampMax="90.0"))
	float ActiveSeekerConeAngle = 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Seeker", meta=(ClampMin="0.0"))
	float SeekerMissedScanGraceSeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Seeker", meta=(ClampMin="0.1"))
	float ActiveSearchTimeoutSeconds = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Seeker", meta=(ClampMin="1.0"))
	float TargetSwitchScoreRatio = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Support", meta=(ClampMin="0.0"))
	float RequiredSupportCoastSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Datalink")
	bool bUseMidCourseDataLink = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Datalink", meta=(ClampMin="0.1"))
	float MidCourseUpdateInterval = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Datalink", meta=(ClampMin="0.1"))
	float MaxMidCourseMeasurementAgeSeconds = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Datalink", meta=(ClampMin="0.0"))
	float MaxMidCourseInnovationCm = 200000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Datalink", meta=(ClampMin="1.0"))
	float DataLinkRange = 10000000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Countermeasures", meta=(ClampMin="0.0"))
	float NotchFilterVelocity = 3000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Countermeasures", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ChaffBreakLockChance = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Countermeasures")
	TArray<FName> ChaffDecoyTags;

	UFUNCTION(BlueprintCallable, Category="Missile|Radar")
	void ReceiveMidCourseUpdate(const FMissileTargetSolution& NewSolution);
	UFUNCTION(BlueprintCallable, Category="Missile|Radar")
	void SetInertialTarget(FVector InPosition, FVector InVelocity);
	void SetParentRadar(UAircraftRadarComponent* Radar);
	void SetRemoteDataLinkSupport(UAircraftRadarComponent* LauncherRadar,
		UAircraftRadarComponent* SourceRadar, int32 SourceParticipantID);
	void ClearRemoteDataLinkSupport(UAircraftRadarComponent* LauncherRadar);
	UFUNCTION(BlueprintPure, Category="Missile|Radar")
	bool IsSeekerEmittingRF() const { return bWeaponFired && bSeekerTransmitting && bIsWeaponActivated; }
	UFUNCTION(BlueprintPure, Category="Missile|Radar")
	ERadarMissileFlightPhase GetFlightPhase() const { return FlightPhase; }
	UFUNCTION(BlueprintPure, Category="Missile|Radar")
	bool IsInTerminalPhase() const { return bSeekerTransmitting; }
	UFUNCTION(BlueprintPure, Category="Missile|Radar")
	UAircraftRadarComponent* GetParentRadar() const;
	UFUNCTION(BlueprintPure, Category="Missile|Radar")
	UAircraftRadarComponent* GetIlluminator() const { return Illuminator.Get(); }
	UFUNCTION(BlueprintPure, Category="Missile|Radar")
	bool HasVerifiedIllumination() const;
	UFUNCTION(BlueprintPure, Category="Missile|Radar")
	bool IsEligibleRadarTarget(const AActor* Candidate) const { return AcceptCandidate(Candidate); }
	UFUNCTION(BlueprintPure, Category="Missile|Radar")
	FVector GetInertialTargetLocation() const { return TargetSolution.Position; }
	UAircraftRadarComponent* GetLaunchRadar() const { return LaunchRadar.Get(); }
	int32 GetLaunchTrackID() const { return LaunchTrackID; }
	bool IsMadDogLaunch() const { return bMadDogLaunch; }
	void FillLaunchedMissileStatus(FRadarLaunchedMissileStatus& OutStatus) const;

	virtual bool PrepareLaunch(const FMissileLaunchConfiguration& Configuration) override;
	virtual bool CanFireWeapon() const override;
	virtual bool FireWeapon() override;
	virtual void ActivateWeapon(bool bActivate = true) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickSeekerLogic(float DeltaTime) override;
	virtual bool GetGuidanceTargetSolution(FMissileTargetSolution& OutSolution) const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool AcceptCandidate(const AActor* Candidate) const;
	virtual bool IsFuzeTargetEligible(const AActor* Candidate) const override { return AcceptCandidate(Candidate); }
	void ActivateOnboardSeeker();
	void ScanActiveSeeker(float DeltaTime);
	void RefreshMidCourseUpdate(float DeltaTime);
	void SetFlightPhase(ERadarMissileFlightPhase NewPhase);
	bool HasLineOfSight(const FVector& From, const AActor* Target,
		const AActor* SourceOwner = nullptr) const;
	bool HasVerifiedIlluminationFrom(const UAircraftRadarComponent* Radar, const AActor* Target) const;
	float GetEstimatedTargetRange() const;
	UAircraftRadarComponent* ResolveExternalRadarForSolution(const FMissileTargetSolution& Solution) const;
	UAircraftRadarComponent* GetCurrentExternalGuidanceRadar() const;
	void ResolveLaunchTrackIdentity();
	UFUNCTION()
	void HandleProximityFuzeTriggered(AActor* TriggeringActor);

	UPROPERTY(Transient, Replicated)
	ERadarMissileFlightPhase FlightPhase = ERadarMissileFlightPhase::PreLaunch;
	UPROPERTY(Transient, Replicated)
	bool bSeekerTransmitting = false;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAircraftRadarComponent> Illuminator;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAircraftRadarComponent> Uplink;
	TWeakObjectPtr<UAircraftRadarComponent> LaunchRadar;
	TWeakObjectPtr<UAircraftRadarComponent> LastExternalGuidanceRadar;
	int32 LaunchTrackID = -1;
	int32 LaunchContactID = 0;
	bool bMadDogLaunch = false;
	float LastExternalUpdateWorldTime = -1000000.0f;
	UPROPERTY(Transient)
	FMissileTargetSolution LastSupportSolution;
	UPROPERTY(Transient)
	float SupportLostSeconds = 0.0f;
	UPROPERTY(Transient)
	float TimeSinceLastDataLink = 0.0f;
	UPROPERTY(Transient)
	float TimeSinceSeekerSawTarget = 0.0f;
	float ActiveSearchElapsedSeconds = 0.0f;
	UPROPERTY(Transient)
	bool bDataLinkTimeoutNotified = false;
	bool bHadUplink = false;
	bool bHasActiveSeeker = false;
	bool bNeedsIllumination = false;
	bool bSeaTargetsOnly = false;
	TSet<TWeakObjectPtr<AActor>> ObservedChaff;
};

/** Autonomous active radar: immediate search or INS/datalink followed by terminal search. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UActiveRadarMissileGuidanceComponent : public URadarMissileGuidanceComponent
{
	GENERATED_BODY()
public:
	UActiveRadarMissileGuidanceComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|Radar|Seeker")
	EActiveSeekerActivation SeekerActivation = EActiveSeekerActivation::AtRange;
	virtual bool FireWeapon() override;
protected:
	virtual void TickSeekerLogic(float DeltaTime) override;
};

/** Passive receiver of a supporting radar's target reflection; never transmits RF. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API USemiActiveRadarMissileGuidanceComponent : public URadarMissileGuidanceComponent
{
	GENERATED_BODY()
public:
	USemiActiveRadarMissileGuidanceComponent();
};

/** SARH first, then autonomous active radar at takeover range. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UHybridRadarMissileGuidanceComponent : public URadarMissileGuidanceComponent
{
	GENERATED_BODY()
public:
	UHybridRadarMissileGuidanceComponent();
};

/** INS/track transit and active sea-domain terminal seeker with sea-skimming. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class WEAPONSYSTEMS_API UAntiShipMissileGuidanceComponent : public URadarMissileGuidanceComponent
{
	GENERATED_BODY()
public:
	UAntiShipMissileGuidanceComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|AntiShip")
	bool bSeaSkim = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|AntiShip")
	float SeaSurfaceWorldZ = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Missile|AntiShip", meta=(ClampMin="100.0"))
	float SeaSkimHeight = 500.0f;
protected:
	virtual void TickSeekerLogic(float DeltaTime) override;
	virtual bool GetGuidanceTargetSolution(FMissileTargetSolution& OutSolution) const override;
};
