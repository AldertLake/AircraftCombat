// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "MissileGuidanceComponent.h"
#include "RadarMissileGuidanceComponent.h"
#include "IRMissileGuidanceComponent.h"
#include "ARMMissileGuidanceComponent.h"
#include "ModularMissionManagement.h"
#include "AircraftRadarComponent.h"
#include "RadarWarningReceiverComponent.h"
#include "AircraftCombatSubsystem.h"
#include "Weapon.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"

namespace
{
	UWorld* MakeGuidanceTestWorld()
	{
		const UWorld::InitializationValues Init = UWorld::InitializationValues()
			.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
			.CreateNavigation(false).CreateAISystem(false).SetTransactional(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
			nullptr, true, ERHIFeatureLevel::Num, &Init);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		return World;
	}

	void FinishGuidanceTestWorld(UWorld* World)
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}

	template <typename T>
	T* AddGuidanceComponent(AActor* Owner)
	{
		T* Component = NewObject<T>(Owner);
		Owner->AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}

	AActor* SpawnGuidanceContact(UWorld* World, const FVector& Position, FName Tag = NAME_None)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(500.0f));
		Box->SetCollisionObjectType(ECC_WorldDynamic);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Overlap);
		Box->RegisterComponent();
		Actor->SetActorLocation(Position);
		if (Tag != NAME_None) Actor->Tags.Add(Tag);
		if (UAircraftCombatSubsystem* Registry = World->GetSubsystem<UAircraftCombatSubsystem>())
			Registry->RegisterCombatActor(Actor);
		return Actor;
	}

	void AdvanceGuidanceWorld(UWorld* World, float Seconds, float Step = 0.1f)
	{
		const int32 Count = FMath::CeilToInt(Seconds / Step);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Step);
			// This lightweight world does not initialize actor tick scheduling as PIE does.
			for (TActorIterator<AWeapon> It(World); It; ++It)
			{
				TInlineComponentArray<UMissileGuidanceComponent*> Missiles(*It);
				for (UMissileGuidanceComponent* Missile : Missiles)
					if (Missile->IsComponentTickEnabled())
						Missile->TickComponent(Step, LEVELTICK_All, nullptr);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissileIndependentLaunchTest,
	"AircraftCombat.Missiles.IndependentLaunchAndSeeker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMissileIndependentLaunchTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeGuidanceTestWorld();
	if (!TestNotNull(TEXT("missile test world"), World)) return false;

	AWeapon* CoordinateActor = World->SpawnActor<AWeapon>();
	CoordinateActor->SetActorLocation(FVector(100000.0f, 100000.0f, 0.0f));
	UActiveRadarMissileGuidanceComponent* CoordinateMissile =
		AddGuidanceComponent<UActiveRadarMissileGuidanceComponent>(CoordinateActor);
	CoordinateMissile->SetUpdatedComponent(CoordinateActor->GetRootComponent());
	CoordinateMissile->InitialSpeed = 30000.0f;
	CoordinateMissile->MotorAcceleration = 0.0f;
	CoordinateMissile->GuidanceActivationDelay = 0.0f;
	CoordinateMissile->ActiveSeekerRange = 1000.0f;
	FMissileLaunchConfiguration OriginLaunch;
	OriginLaunch.Target.bValid = true;
	OriginLaunch.Target.Position = FVector::ZeroVector;

	AWeapon* IRActor = World->SpawnActor<AWeapon>();
	UIRMissileGuidanceComponent* IR = AddGuidanceComponent<UIRMissileGuidanceComponent>(IRActor);
	IR->MaxSensorRange = 1000.0f;
	AActor* FarTarget = SpawnGuidanceContact(World, FVector(100000.0f, 0.0f, 0.0f));
	FMissileLaunchConfiguration IRCue;
	IRCue.Target.bValid = true;
	IRCue.Target.TargetActor = FarTarget;

	APawn* Carrier = World->SpawnActor<APawn>();
	USceneComponent* CarrierRoot = NewObject<USceneComponent>(Carrier);
	Carrier->SetRootComponent(CarrierRoot);
	CarrierRoot->RegisterComponent();
	UAircraftRadarComponent* LauncherRadar = AddGuidanceComponent<UAircraftRadarComponent>(Carrier);
	LauncherRadar->RadarMode = ERadarOperatingMode::Standby;
	OriginLaunch.LaunchRadar = LauncherRadar;
	AWeapon* MountedIRActor = World->SpawnActor<AWeapon>();
	MountedIRActor->AttachToActor(Carrier, FAttachmentTransformRules::KeepWorldTransform);
	MountedIRActor->SetMounted(true, Carrier);
	UIRMissileGuidanceComponent* MountedIR = AddGuidanceComponent<UIRMissileGuidanceComponent>(MountedIRActor);
	MountedIR->MaxSensorRange = 1000.0f;
	UModularMissionManagement* Mission = AddGuidanceComponent<UModularMissionManagement>(Carrier);
	Mission->bAutoSpawnStoresOnBeginPlay = false;
	Mission->bAutoStepStationOnFire = false;
	Mission->MasterArmMode = EMasterArmMode::Arm;
	FWeaponStation Station;
	Station.StationIndex = 1;
	Station.StoreType = EStoreType::AirToAirMissile_IR;
	FStationStore Store;
	Store.MountedActor = MountedIRActor;
	Station.Stores.Add(Store);
	Mission->Stations.Add(Station);

	AActor* Ship = SpawnGuidanceContact(World, FVector(40000.0f, 0.0f, 0.0f), TEXT("Ship"));
	AActor* Air = SpawnGuidanceContact(World, FVector(50000.0f, 0.0f, 0.0f));
	AWeapon* ShipMissileActor = World->SpawnActor<AWeapon>();
	UAntiShipMissileGuidanceComponent* ShipMissile =
		AddGuidanceComponent<UAntiShipMissileGuidanceComponent>(ShipMissileActor);

	World->BeginPlay();
	CoordinateMissile->ActivateWeapon(true);
	TestTrue(TEXT("standalone coordinate at world origin prepares"), CoordinateMissile->PrepareLaunch(OriginLaunch));
	TestTrue(TEXT("standalone missile launches without SMS"), CoordinateMissile->FireWeapon());
	TArray<FRadarLaunchedMissileStatus> StandaloneLaunched;
	LauncherRadar->GetLaunchedRadarMissiles(StandaloneLaunched);
	TestTrue(TEXT("coordinate launch has no track and no invented impact estimate"),
		StandaloneLaunched.Num() == 1 &&
		StandaloneLaunched[0].MissileComponent == CoordinateMissile &&
		StandaloneLaunched[0].LaunchTrackID == -1 && !StandaloneLaunched[0].bMadDog &&
		StandaloneLaunched[0].EstimatedTimeToImpactSeconds == -1.0f);
	AWeapon* MadDogActor = World->SpawnActor<AWeapon>();
	UActiveRadarMissileGuidanceComponent* MadDog =
		AddGuidanceComponent<UActiveRadarMissileGuidanceComponent>(MadDogActor);
	MadDog->SetUpdatedComponent(MadDogActor->GetRootComponent());
	MadDog->SeekerActivation = EActiveSeekerActivation::Immediate;
	MadDog->bRequireLockToFire = false;
	MadDog->ActivateWeapon(true);
	FMissileLaunchConfiguration MadDogLaunch;
	MadDogLaunch.LaunchRadar = LauncherRadar;
	MadDogLaunch.bMadDog = true;
	TestTrue(TEXT("mad dog launch succeeds without a target solution"),
		MadDog->PrepareLaunch(MadDogLaunch) && MadDog->FireWeapon());
	LauncherRadar->GetLaunchedRadarMissiles(StandaloneLaunched);
	TestTrue(TEXT("mad dog status safely marks unsupported estimates"),
		StandaloneLaunched.ContainsByPredicate([MadDog, LauncherRadar](const FRadarLaunchedMissileStatus& Status)
		{
			return Status.MissileComponent == MadDog && Status.LaunchRadar == LauncherRadar &&
				Status.bMadDog && Status.LaunchTrackID == -1 &&
				Status.GuidanceSource == ERadarMissileGuidanceSource::OnboardSeeker &&
				Status.TimeToActiveSeconds == -1.0f &&
				Status.EstimatedTimeToImpactSeconds == -1.0f;
		}));
	TestFalse(TEXT("same missile cannot launch twice"), CoordinateMissile->FireWeapon());
	CoordinateMissile->UpdateGuidanceVelocity(0.1f);
	TestTrue(TEXT("coordinate-only steering turns toward world origin"), CoordinateMissile->Velocity.Y < -1.0f);
	CoordinateMissile->bUseStagedMotorProfile = true;
	CoordinateMissile->MotorIgnitionDelay = 0.0f;
	CoordinateMissile->MotorAcceleration = 1000.0f;
	CoordinateMissile->BoostDurationSeconds = 0.3f;
	CoordinateMissile->SustainAcceleration = 0.0f;
	CoordinateMissile->SustainDurationSeconds = 0.0f;
	CoordinateMissile->QuadraticDragCoefficient = 0.0f;
	const float SpeedBeforeBoost = CoordinateMissile->Velocity.Size();
	CoordinateMissile->UpdateGuidanceVelocity(0.1f);
	const float SpeedDuringBoost = CoordinateMissile->Velocity.Size();
	CoordinateMissile->UpdateGuidanceVelocity(0.3f);
	TestTrue(TEXT("staged motor boosts during configured burn"), SpeedDuringBoost > SpeedBeforeBoost);
	TestTrue(TEXT("staged motor coasts without phantom thrust after burnout"),
		FMath::IsNearlyEqual(CoordinateMissile->Velocity.Size(), SpeedDuringBoost, 1.0f));

	TestFalse(TEXT("out-of-range IR designation is rejected"), IR->PrepareLaunch(IRCue));
	TestNull(TEXT("failed IR cue does not force a lock"), IR->GetLockedTarget());
	Mission->SetDesignatedTarget(FarTarget);
	TestFalse(TEXT("SMS reports a failed seeker launch"), Mission->Fire(1));
	FWeaponStation Unchanged;
	TestTrue(TEXT("station still exists"), Mission->GetStation(1, Unchanged));
	TestEqual(TEXT("failed launch keeps ammunition"), Unchanged.GetCurrentAmmo(), 1);
	TestTrue(TEXT("failed launch keeps mounted actor"),
		Unchanged.Stores.Num() == 1 && Unchanged.Stores[0].MountedActor == MountedIRActor);
	TestTrue(TEXT("failed launch leaves store attached"), MountedIRActor->GetAttachParentActor() == Carrier);
	TestFalse(TEXT("failed launch does not fire the missile"), MountedIR->IsWeaponFired());

	TestTrue(TEXT("anti-ship seeker accepts maritime domain"), ShipMissile->IsEligibleRadarTarget(Ship));
	TestFalse(TEXT("anti-ship seeker rejects air domain"), ShipMissile->IsEligibleRadarTarget(Air));
	ShipMissile->SeaSurfaceWorldZ = 12000.0f;
	ShipMissile->SeaSkimHeight = 700.0f;
	ShipMissile->ActivateWeapon(true);
	FMissileLaunchConfiguration ShipLaunch;
	ShipLaunch.Target.bValid = true;
	ShipLaunch.Target.Position = FVector(40000.0f, 0.0f, 0.0f);
	TestTrue(TEXT("anti-ship coordinate transit launches"), ShipMissile->PrepareLaunch(ShipLaunch) &&
		ShipMissile->FireWeapon());
	AdvanceGuidanceWorld(World, 0.1f);
	TestTrue(TEXT("anti-ship terminal seeker acquires sea target"),
		ShipMissile->GetLockedTarget() == Ship);
	TestEqual(TEXT("sea skim keeps original target measurement"),
		ShipMissile->GetTargetSolution().Position.Z, 0.0);
	FinishGuidanceTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadarMissileSupportTest,
	"AircraftCombat.Missiles.SupportAndTakeover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadarMissileSupportTest::RunTest(const FString& Parameters)
{
	UWorld* World = MakeGuidanceTestWorld();
	if (!TestNotNull(TEXT("support test world"), World)) return false;

	AActor* Platform = World->SpawnActor<AActor>();
	USceneComponent* PlatformRoot = NewObject<USceneComponent>(Platform);
	Platform->SetRootComponent(PlatformRoot);
	PlatformRoot->RegisterComponent();
	UAircraftRadarComponent* Radar = AddGuidanceComponent<UAircraftRadarComponent>(Platform);
	Radar->ScanDrive = ERadarScanDrive::AESA;
	Radar->AESABeamsPerSample = 16;
	Radar->AzimuthScanWidth = 360.0f;
	Radar->ElevationBars = 1;
	Radar->BeamAzimuthWidth = 45.0f;
	Radar->BeamElevationWidth = 45.0f;
	Radar->MaxDetectionRange = 1000000.0f;
	Radar->MinDetectionRange = 0.0f;
	Radar->ScanSampleInterval = 0.05f;
	Radar->bEnablePhysicsCandidateDiscovery = false;
	Radar->bEnableTerrainMasking = false;
	AActor* Target = SpawnGuidanceContact(World, FVector(100000.0f, 0.0f, 0.0f));
	URadarWarningReceiverComponent* RWR = AddGuidanceComponent<URadarWarningReceiverComponent>(Target);
	RWR->bEnableIFF = false;
	RWR->bEnableTerrainMasking = false;
	RWR->RWRUpdateInterval = 0.1f;

	World->BeginPlay();
	Platform->DispatchBeginPlay();
	AdvanceGuidanceWorld(World, 2.0f);
	FRadarTrack Track;
	if (!TestTrue(TEXT("support radar has a fresh target track"), Radar->GetTrackByActor(Target, Track)))
	{
		FinishGuidanceTestWorld(World);
		return false;
	}
	FMissileLaunchConfiguration Launch;
	Launch.Target.bValid = true;
	Launch.Target.bMeasured = true;
	Launch.Target.TargetActor = Target;
	Launch.Target.Position = Track.LastKnownPosition;
	Launch.Target.Velocity = Track.EstimatedVelocity;
	Launch.Target.MeasurementTimeSeconds = World->GetTimeSeconds() - Track.TrackAge;
	Launch.Target.TargetContactID = Track.ContactID;
	Launch.LaunchRadar = Radar;
	Launch.LaunchTrackID = Track.TrackID;
	Launch.Illuminator = Radar;
	Radar->SetContinuousWaveIllumination(Target, true);

	AWeapon* PassiveActor = World->SpawnActor<AWeapon>();
	PassiveActor->SetActorLocation(FVector(20000.0f, 0.0f, 0.0f));
	USemiActiveRadarMissileGuidanceComponent* Passive =
		AddGuidanceComponent<USemiActiveRadarMissileGuidanceComponent>(PassiveActor);
	Passive->SetUpdatedComponent(PassiveActor->GetRootComponent());
	Passive->MotorAcceleration = 0.0f;
	Passive->InitialSpeed = 1000.0f;
	Passive->ActivateWeapon(true);
	TestTrue(TEXT("passive missile accepts real illumination"), Passive->PrepareLaunch(Launch) &&
		Passive->CanFireWeapon());
	TestTrue(TEXT("passive missile launches with actual illumination"), Passive->FireWeapon());
	TArray<FRadarLaunchedMissileStatus> Launched;
	Radar->GetLaunchedRadarMissiles(Launched);
	TestTrue(TEXT("launch radar registers successful semi-active launch"),
		Launched.Num() == 1 && Launched[0].MissileComponent == Passive &&
		Launched[0].LaunchTrackID == Track.TrackID && Launched[0].ContactID == Track.ContactID &&
		Launched[0].LaunchRadar == Radar && Launched[0].RelevantRadar == Radar &&
		Launched[0].GuidanceSource == ERadarMissileGuidanceSource::Illumination &&
		Launched[0].TimeToActiveSeconds == -1.0f &&
		Launched[0].EstimatedTimeToImpactSeconds > 0.0f);
	TestFalse(TEXT("passive missile never transmits"), Passive->IsSeekerEmittingRF());
	static_cast<UActorComponent*>(RWR)->TickComponent(0.2f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("supporting radar reports SARH launch warning"),
		RWR->GetAllThreats().ContainsByPredicate([Platform](const FRWRThreatEntry& Threat)
		{
			return Threat.SourceActor == Platform && Threat.ThreatType == ERWRThreatType::MissileLaunch;
		}));
	Radar->SetContinuousWaveIllumination(Target, false);
	TestFalse(TEXT("registration cannot synthesize CW illumination"), Radar->IsContinuousWaveIlluminating(Target));
	AdvanceGuidanceWorld(World, 0.5f);
	TestEqual(TEXT("passive missile coasts on the last measured track"),
		Passive->GetFlightPhase(), ERadarMissileFlightPhase::Coasting);
	Radar->SetContinuousWaveIllumination(Target, true);
	AdvanceGuidanceWorld(World, 0.2f);
	TestEqual(TEXT("passive missile recovers illumination during coast"),
		Passive->GetFlightPhase(), ERadarMissileFlightPhase::SemiActive);
	Radar->SetContinuousWaveIllumination(Target, false);
	AdvanceGuidanceWorld(World, 1.2f);
	TestEqual(TEXT("passive missile still coasts before timeout"),
		Passive->GetFlightPhase(), ERadarMissileFlightPhase::Coasting);
	AdvanceGuidanceWorld(World, 0.5f);
	TestEqual(TEXT("passive missile becomes unguided after support timeout"),
		Passive->GetFlightPhase(), ERadarMissileFlightPhase::Unguided);
	Radar->GetLaunchedRadarMissiles(Launched);
	TestTrue(TEXT("unguided missile remains registered until spent"),
		Launched.Num() == 1 &&
		Launched[0].GuidanceSource == ERadarMissileGuidanceSource::Unguided &&
		Launched[0].EstimatedTimeToImpactSeconds == -1.0f);

	Radar->SetContinuousWaveIllumination(Target, true);
	AWeapon* HybridActor = World->SpawnActor<AWeapon>();
	HybridActor->SetActorLocation(FVector(20000.0f, 0.0f, 0.0f));
	UHybridRadarMissileGuidanceComponent* Hybrid =
		AddGuidanceComponent<UHybridRadarMissileGuidanceComponent>(HybridActor);
	Hybrid->SetUpdatedComponent(HybridActor->GetRootComponent());
	Hybrid->MotorAcceleration = 0.0f;
	Hybrid->InitialSpeed = 1000.0f;
	Hybrid->ActiveSeekerRange = 100000.0f;
	Hybrid->NotchFilterVelocity = 0.0f;
	Hybrid->ActivateWeapon(true);
	TestTrue(TEXT("hybrid prepares from illuminated track"), Hybrid->PrepareLaunch(Launch));
	TestTrue(TEXT("hybrid launches while passively guided"), Hybrid->FireWeapon());
	AdvanceGuidanceWorld(World, 0.2f);
	TestTrue(TEXT("hybrid onboard radar transmits after takeover"), Hybrid->IsSeekerEmittingRF());
	static_cast<UActorComponent*>(RWR)->TickComponent(0.2f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("active missile appears as an independent RWR source"),
		RWR->GetAllThreats().ContainsByPredicate([HybridActor](const FRWRThreatEntry& Threat)
		{
			return Threat.SourceActor == HybridActor && Threat.ThreatType == ERWRThreatType::MissileSeeker;
		}));
	TestFalse(TEXT("hybrid releases supporting radar after takeover"),
		Radar->GetActiveGuidingMissiles().Contains(Hybrid));
	Radar->GetLaunchedRadarMissiles(Launched);
	TestTrue(TEXT("launcher still lists hybrid after onboard takeover"),
		Launched.Num() == 2 && Launched.ContainsByPredicate([Hybrid, Radar](const FRadarLaunchedMissileStatus& Status)
		{
			return Status.MissileComponent == Hybrid &&
				Status.CurrentExternalRadar == nullptr && Status.LastExternalRadar == Radar &&
				Status.RelevantRadar == Radar && Status.bOnboardSeekerActive &&
				Status.TimeToActiveSeconds == 0.0f;
		}));
	FMissileTargetSolution LateExternalUpdate = Hybrid->GetTargetSolution();
	LateExternalUpdate.SourceParticipantID = 12345;
	LateExternalUpdate.SourceTrackID = 42;
	LateExternalUpdate.MeasurementTimeSeconds = World->GetTimeSeconds();
	Hybrid->ReceiveMidCourseUpdate(LateExternalUpdate);
	Radar->GetLaunchedRadarMissiles(Launched);
	TestTrue(TEXT("onboard takeover freezes last external radar provenance"),
		Launched.ContainsByPredicate([Hybrid, Radar](const FRadarLaunchedMissileStatus& Status)
			{ return Status.MissileComponent == Hybrid && Status.LastExternalRadar == Radar; }));
	TestTrue(TEXT("hybrid terminal seeker acquires intended target"), Hybrid->GetLockedTarget() == Target);
	Target->SetActorLocation(FVector(103000.0f, 0.0f, 0.0f));
	SpawnGuidanceContact(World, FVector(100000.0f, 0.0f, 0.0f));
	AdvanceGuidanceWorld(World, 0.1f);
	TestTrue(TEXT("seeker retains target over a marginally better contact"),
		Hybrid->GetLockedTarget() == Target);
	Hybrid->ChaffDecoyTags.Add(TEXT("Chaff"));
	Hybrid->ChaffBreakLockChance = 1.0f;
	AActor* Chaff = SpawnGuidanceContact(World, FVector(101000.0f, 0.0f, 0.0f), TEXT("Chaff"));
	AdvanceGuidanceWorld(World, 0.1f);
	TestTrue(TEXT("new in-gate chaff can seduce a radar seeker"),
		Hybrid->GetLockedTarget() == Chaff);

	const int32 RadarSeekerCount = World->GetSubsystem<UAircraftCombatSubsystem>()->
		GetRegisteredMissileSeekers().Num();
	AWeapon* ARMActor = World->SpawnActor<AWeapon>();
	ARMActor->SetActorLocation(FVector(150000.0f, 0.0f, 0.0f));
	UARMMissileGuidanceComponent* ARM = AddGuidanceComponent<UARMMissileGuidanceComponent>(ARMActor);
	ARM->SetUpdatedComponent(ARMActor->GetRootComponent());
	ARM->MotorAcceleration = 0.0f;
	ARM->InitialSpeed = 1000.0f;
	ARM->bEnableLoft = false;
	ARM->ActivateWeapon(true);
	ARM->HandoffEmitter(Platform, Radar);
	TestTrue(TEXT("ARM independently launches on target emissions"), ARM->FireWeapon());
	TestEqual(TEXT("ARM does not register as an active radar seeker"),
		World->GetSubsystem<UAircraftCombatSubsystem>()->GetRegisteredMissileSeekers().Num(),
		RadarSeekerCount);
	Radar->SetRadarMode(ERadarOperatingMode::Off);
	AdvanceGuidanceWorld(World, 1.0f);
	TestEqual(TEXT("ARM uses memory after emitter shutdown"), ARM->GetFlightPhase(),
		EARMFlightPhase::DeadReckoning);
	Radar->SetRadarMode(ERadarOperatingMode::Search);
	AdvanceGuidanceWorld(World, 0.2f);
	TestEqual(TEXT("ARM reacquires resumed emission"), ARM->GetFlightPhase(),
		EARMFlightPhase::TerminalTracking);

	AWeapon* ActiveActor = World->SpawnActor<AWeapon>();
	ActiveActor->SetActorLocation(FVector(20000.0f, 0.0f, 0.0f));
	UActiveRadarMissileGuidanceComponent* Active =
		AddGuidanceComponent<UActiveRadarMissileGuidanceComponent>(ActiveActor);
	Active->SetUpdatedComponent(ActiveActor->GetRootComponent());
	Active->MotorAcceleration = 0.0f;
	Active->InitialSpeed = 1000.0f;
	Active->ActiveSeekerRange = 1000.0f;
	Active->MidCourseUpdateInterval = 0.1f;
	Active->ActivateWeapon(true);
	FMissileLaunchConfiguration ActiveLaunch = Launch;
	ActiveLaunch.Target.SourceTrackID = Track.TrackID;
	ActiveLaunch.Uplink = Radar;
	ActiveLaunch.Illuminator = nullptr;
	TestTrue(TEXT("active missile launches from a measured solution"),
		Active->PrepareLaunch(ActiveLaunch) && Active->FireWeapon());
	Radar->GetLaunchedRadarMissiles(Launched);
	TestTrue(TEXT("active missile appears in launcher registry"),
		Launched.ContainsByPredicate([Active](const FRadarLaunchedMissileStatus& Status)
			{ return Status.MissileComponent == Active && Status.TimeToActiveSeconds > 0.0f; }));
	FMissileTargetSolution WrongTrackUpdate = Active->GetTargetSolution();
	WrongTrackUpdate.SourceTrackID += 1;
	WrongTrackUpdate.Position = FVector(999999.0f, 0.0f, 0.0f);
	WrongTrackUpdate.MeasurementTimeSeconds = World->GetTimeSeconds() + 1.0f;
	Active->ReceiveMidCourseUpdate(WrongTrackUpdate);
	TestTrue(TEXT("midcourse rejects another track identity"),
		Active->GetTargetSolution().Position != WrongTrackUpdate.Position);
	Radar->SetRadarMode(ERadarOperatingMode::Off);
	AdvanceGuidanceWorld(World, 2.0f);
	TestEqual(TEXT("active missile keeps its estimate after optional uplink loss"),
		Active->GetFlightPhase(), ERadarMissileFlightPhase::MidCourse);
	Radar->GetLaunchedRadarMissiles(Launched);
	TestTrue(TEXT("uplink loss reports inertial flight and retains the last provider"),
		Launched.ContainsByPredicate([Active, Radar](const FRadarLaunchedMissileStatus& Status)
		{
			return Status.MissileComponent == Active &&
				Status.GuidanceSource == ERadarMissileGuidanceSource::Inertial &&
				Status.CurrentExternalRadar == nullptr && Status.LastExternalRadar == Radar;
		}));
	Active->FuzeArmingDelay = 0.0f;
	ActiveActor->SetActorLocation(Target->GetActorLocation());
	TestTrue(TEXT("radar missile proximity fuze triggers near contact"), Active->CheckProximityFuze());
	Radar->GetLaunchedRadarMissiles(Launched);
	TestFalse(TEXT("proximity fuze event removes spent missile immediately"),
		Launched.ContainsByPredicate([Active](const FRadarLaunchedMissileStatus& Status)
			{ return Status.MissileComponent == Active; }));
	HybridActor->Destroy();
	Radar->GetLaunchedRadarMissiles(Launched);
	TestFalse(TEXT("destroyed missile is removed from launcher registry"),
		Launched.ContainsByPredicate([Hybrid](const FRadarLaunchedMissileStatus& Status)
			{ return Status.MissileComponent == Hybrid; }));
	Passive->ActivateWeapon(false);
	Radar->GetLaunchedRadarMissiles(Launched);
	TestTrue(TEXT("deactivated last missile clears launcher registry"), Launched.IsEmpty());
	FinishGuidanceTestWorld(World);
	return true;
}
#endif

