// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftDataLinkSubsystem.h"
#include "AircraftRadarComponent.h"
#include "AircraftCombatSubsystem.h"
#include "RadarMissileGuidanceComponent.h"
#include "Weapon.h"
#include "IFFTransponderComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAircraftDataLinkRoutingTest,
	"AircraftCombat.DataLink.RoutingAndIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAircraftDataLinkRoutingTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("data link world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto SpawnRadar = [World](const FVector& Position, FName Network, uint8 Team)
	{
		AActor* Platform = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Platform);
		Platform->SetRootComponent(Root);
		Root->RegisterComponent();
		Platform->SetActorLocation(Position);
		UAircraftRadarComponent* Radar = NewObject<UAircraftRadarComponent>(Platform);
		Platform->AddInstanceComponent(Radar);
		Radar->bEnableDataLink = true;
		Radar->DataLinkNetworkID = Network;
		Radar->DataLinkRangeCm = 120000.0f;
		Radar->TeamID = Team;
		Radar->bEnablePhysicsCandidateDiscovery = false;
		Radar->bEnableTargetRCSTagParsing = false;
		Radar->bEnableTerrainMasking = false;
		Radar->RegisterComponent();
		return Radar;
	};
	UAircraftRadarComponent* A = SpawnRadar(FVector::ZeroVector, TEXT("Blue"), 1);
	UAircraftRadarComponent* B = SpawnRadar(FVector(100000.0f, 0.0f, 0.0f), TEXT("Blue"), 1);
	UAircraftRadarComponent* C = SpawnRadar(FVector(200000.0f, 0.0f, 0.0f), TEXT("Blue"), 1);
	UAircraftRadarComponent* Hostile = SpawnRadar(FVector(50000.0f, 50000.0f, 0.0f), TEXT("Blue"), 2);
	UAircraftRadarComponent* OtherNet = SpawnRadar(FVector(50000.0f, -50000.0f, 0.0f), TEXT("Red"), 1);
	A->ScanDrive = ERadarScanDrive::AESA;
	A->AESABeamsPerSample = 16;
	A->AzimuthScanWidth = 360.0f;
	A->ElevationBars = 1;
	A->BeamAzimuthWidth = 45.0f;
	A->BeamElevationWidth = 45.0f;
	A->MaxDetectionRange = 1000000.0f;
	A->MinDetectionRange = 0.0f;
	A->ScanSampleInterval = 0.05f;
	A->bRestrictDataLinkToFriendly = true;
	A->DataLinkPlatformType = EDataLinkPlatformType::Fighter;
	B->RadarMode = ERadarOperatingMode::Standby;
	C->RadarMode = ERadarOperatingMode::Standby;
	Hostile->RadarMode = ERadarOperatingMode::Standby;
	OtherNet->RadarMode = ERadarOperatingMode::Standby;
	B->bRelayDataLinkReports = true;
	AActor* Target = World->SpawnActor<AActor>();
	UBoxComponent* TargetBox = NewObject<UBoxComponent>(Target);
	Target->SetRootComponent(TargetBox);
	TargetBox->SetBoxExtent(FVector(1000.0f));
	TargetBox->SetCollisionObjectType(ECC_WorldDynamic);
	TargetBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TargetBox->SetCollisionResponseToAllChannels(ECR_Overlap);
	TargetBox->RegisterComponent();
	Target->SetActorLocation(FVector(50000.0f, 0.0f, 0.0f));
	if (UAircraftCombatSubsystem* Registry = World->GetSubsystem<UAircraftCombatSubsystem>())
		Registry->RegisterCombatActor(Target);
	World->BeginPlay();
	for (UAircraftRadarComponent* Radar : {A, B, C, Hostile, OtherNet})
		Radar->GetOwner()->DispatchBeginPlay();
	for (int32 Index = 0; Index < 20; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	FRadarTrack Local;
	TestTrue(TEXT("source has a local measurement"), A->GetTrackByActor(Target, Local));
	TArray<FRadarTrack> Linked;
	C->GetLinkedTracks(Linked);
	TestTrue(TEXT("two-hop receiver sees source report"), Linked.ContainsByPredicate([A](const FRadarTrack& Track)
		{ return Track.SourceParticipantID == A->GetDataLinkParticipantID(); }));
	FRadarTrack RemoteTarget;
	TestTrue(TEXT("original source survives relay"), C->GetFreshLinkedTrackForActor(Target,
		A->GetDataLinkParticipantID(), RemoteTarget) &&
		C->GetLinkedTrackSource(RemoteTarget.TrackID) == A &&
		RemoteTarget.Source == ERadarTrackSource::Fighter);
	FRadarTrack OwnshipRaw;
	OwnshipRaw.LastKnownPosition = B->GetOwner()->GetActorLocation();
	FRadarTrack OwnshipReport;
	TestTrue(TEXT("donor tracks the relay aircraft"),
		A->GetTrackByID(A->CreateTrack(B->GetOwner(), OwnshipRaw), OwnshipReport));
	B->ReceiveDataLinkReport(OwnshipReport, A, World->GetTimeSeconds(), World->GetTimeSeconds());
	B->GetLinkedTracks(Linked);
	TestFalse(TEXT("direct ingress rejects a donated track of the receiver"),
		Linked.ContainsByPredicate([B](const FRadarTrack& Track)
			{ return Track.TrackedActor.Get() == B->GetOwner(); }));
	for (int32 Index = 0; Index < 10; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	B->GetLinkedTracks(Linked);
	TestFalse(TEXT("scheduled transmission omits the receiver's own track"),
		Linked.ContainsByPredicate([B](const FRadarTrack& Track)
			{ return Track.TrackedActor.Get() == B->GetOwner(); }));
	TestTrue(TEXT("self-track filtering does not break network heartbeat"), B->IsDataLinkConnected());
	TestTrue(TEXT("receiver still gets reports about other aircraft"),
		Linked.ContainsByPredicate([Target](const FRadarTrack& Track)
			{ return Track.TrackedActor.Get() == Target; }));
	C->GetLinkedTracks(Linked);
	TestTrue(TEXT("other recipients still get the report about B"),
		Linked.ContainsByPredicate([B](const FRadarTrack& Track)
			{ return Track.TrackedActor.Get() == B->GetOwner(); }));
	AActor* MissileActor = World->SpawnActor<AActor>();
	USceneComponent* MissileRoot = NewObject<USceneComponent>(MissileActor);
	MissileActor->SetRootComponent(MissileRoot);
	MissileRoot->RegisterComponent();
	MissileActor->SetActorLocation(C->GetOwner()->GetActorLocation());
	UActiveRadarMissileGuidanceComponent* Missile = NewObject<UActiveRadarMissileGuidanceComponent>(MissileActor);
	MissileActor->AddInstanceComponent(Missile);
	Missile->RegisterComponent();
	Missile->GuidanceActivationDelay = 0.0f;
	Missile->MotorIgnitionDelay = 0.0f;
	Missile->MaxCruiseSpeed = 50000.0f;
	Missile->Velocity = FVector(30000.0f, 0.0f, 0.0f);
	Missile->LockMissile(Target);
	Missile->SetRemoteDataLinkSupport(C, A, A->GetDataLinkParticipantID());
	Missile->SetInertialTarget(MissileActor->GetActorLocation() + FVector(100000.0f, 100000.0f, 0.0f),
		FVector::ZeroVector);
	TestTrue(TEXT("active missile uses launcher uplink"), Missile->GetParentRadar() == C);
	Missile->UpdateGuidanceVelocity(0.2f);
	TestTrue(TEXT("remote midcourse steering uses linked state instead of actor truth"), Missile->Velocity.Y > 0.0f);
	USemiActiveRadarMissileGuidanceComponent* PassiveMissile =
		NewObject<USemiActiveRadarMissileGuidanceComponent>(MissileActor);
	MissileActor->AddInstanceComponent(PassiveMissile);
	PassiveMissile->RegisterComponent();
	PassiveMissile->ActivateWeapon(true);
	PassiveMissile->LockMissile(Target);
	PassiveMissile->SetRemoteDataLinkSupport(C, A, A->GetDataLinkParticipantID());
	TestTrue(TEXT("semi-active missile uses the original illuminator"), PassiveMissile->GetParentRadar() == A);
	TestFalse(TEXT("semi-active missile rejects launch without source illumination"), PassiveMissile->CanFireWeapon());
	C->bAllowRemoteWeaponSupport = true;
	TestTrue(TEXT("linked target can be selected only while report is fresh"),
		C->DesignateLinkedTrack(RemoteTarget.TrackID) &&
		C->GetSelectedLinkedTrackForActor(Target, RemoteTarget));
	const int32 StableContactID = RemoteTarget.ContactID;
	TestTrue(TEXT("donated report receives a contact identity"), StableContactID > 0);
	AWeapon* CorrelationMissileActor = World->SpawnActor<AWeapon>();
	CorrelationMissileActor->SetActorLocation(C->GetOwner()->GetActorLocation());
	UActiveRadarMissileGuidanceComponent* CorrelationMissile =
		NewObject<UActiveRadarMissileGuidanceComponent>(CorrelationMissileActor);
	CorrelationMissileActor->AddInstanceComponent(CorrelationMissile);
	CorrelationMissile->RegisterComponent();
	CorrelationMissile->SetUpdatedComponent(CorrelationMissileActor->GetRootComponent());
	CorrelationMissile->MotorAcceleration = 0.0f;
	CorrelationMissile->ActiveSeekerRange = 1000.0f;
	CorrelationMissile->MidCourseUpdateInterval = 0.1f;
	CorrelationMissile->ActivateWeapon(true);
	FMissileLaunchConfiguration CorrelatedLaunch;
	CorrelatedLaunch.Target.bValid = true;
	CorrelatedLaunch.Target.bMeasured = true;
	CorrelatedLaunch.Target.TargetActor = Target;
	CorrelatedLaunch.Target.TargetContactID = StableContactID;
	CorrelatedLaunch.Target.SourceParticipantID = RemoteTarget.SourceParticipantID;
	CorrelatedLaunch.Target.SourceTrackID = RemoteTarget.SourceTrackID;
	CorrelatedLaunch.Target.Position = RemoteTarget.LastKnownPosition;
	CorrelatedLaunch.Target.Velocity = RemoteTarget.EstimatedVelocity;
	CorrelatedLaunch.Target.MeasurementTimeSeconds = World->GetTimeSeconds() - RemoteTarget.TrackAge;
	CorrelatedLaunch.LaunchRadar = C;
	CorrelatedLaunch.LaunchTrackID = RemoteTarget.TrackID;
	CorrelatedLaunch.Uplink = C;
	CorrelatedLaunch.Illuminator = A;
	TestTrue(TEXT("active weapon launches on donated measurement"),
		CorrelationMissile->PrepareLaunch(CorrelatedLaunch) && CorrelationMissile->FireWeapon());
	TArray<FRadarLaunchedMissileStatus> LaunchedMissiles;
	C->GetLaunchedRadarMissiles(LaunchedMissiles);
	TestTrue(TEXT("launcher lists donor-supported missile with stable launch identity"),
		LaunchedMissiles.ContainsByPredicate([CorrelationMissile, C, A, RemoteTarget](
			const FRadarLaunchedMissileStatus& Status)
		{
			return Status.MissileComponent == CorrelationMissile && Status.LaunchRadar == C &&
				Status.RelevantRadar == A && Status.LaunchTrackID == RemoteTarget.TrackID &&
				Status.ContactID == RemoteTarget.ContactID;
		}));
	FRadarTrack LocalRaw;
	LocalRaw.LastKnownPosition = Target->GetActorLocation();
	LocalRaw.TrackedActor = Target;
	const int32 LocalTrackID = C->CreateTrack(Target, LocalRaw);
	FRadarTrack LocalTrack;
	TestTrue(TEXT("local measurement exists"), C->GetTrackByID(LocalTrackID, LocalTrack));
	TestEqual(TEXT("local and donor measurements share stable contact identity"),
		LocalTrack.ContactID, StableContactID);
	TArray<FRadarTrack> CorrelatedDisplay;
	C->GetDisplayTracks(CorrelatedDisplay);
	TestEqual(TEXT("one symbol remains for the correlated contact"),
		CorrelatedDisplay.FilterByPredicate([StableContactID](const FRadarTrack& Track)
			{ return Track.ContactID == StableContactID; }).Num(), 1);
	TestTrue(TEXT("fresh local return drives displayed contact"),
		CorrelatedDisplay.ContainsByPredicate([StableContactID](const FRadarTrack& Track)
			{ return Track.ContactID == StableContactID && Track.Source == ERadarTrackSource::Local; }));
	TestTrue(TEXT("designation of donor resolves to fresh local track"),
		C->DesignateLinkedTrack(RemoteTarget.TrackID) && C->GetSelectedLinkedTrackID() == -1);
	TestEqual(TEXT("selection retains contact identity on local acquisition"),
		C->GetSelectedContactID(), StableContactID);
	CorrelationMissile->TickComponent(0.2f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("active missile accepts verified local midcourse source"),
		CorrelationMissile->GetTargetSolution().SourceParticipantID, 0);
	C->GetLaunchedRadarMissiles(LaunchedMissiles);
	TestTrue(TEXT("status changes provider to launching radar after local handover"),
		LaunchedMissiles.ContainsByPredicate([CorrelationMissile, C](const FRadarLaunchedMissileStatus& Status)
			{ return Status.MissileComponent == CorrelationMissile && Status.RelevantRadar == C; }));
	for (int32 Index = 0; Index < 20; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
		CorrelationMissile->TickComponent(0.1f, LEVELTICK_All, nullptr);
	}
	C->GetDisplayTracks(CorrelatedDisplay);
	TestTrue(TEXT("fresh donor resumes display when local measurement ages"),
		CorrelatedDisplay.ContainsByPredicate([StableContactID](const FRadarTrack& Track)
			{ return Track.ContactID == StableContactID && Track.Source != ERadarTrackSource::Local; }));
	TestEqual(TEXT("missile returns to donor without changing target identity"),
		CorrelationMissile->GetTargetSolution().SourceParticipantID, A->GetDataLinkParticipantID());
	C->GetLaunchedRadarMissiles(LaunchedMissiles);
	TestTrue(TEXT("status restores donor provider without moving launch ownership"),
		LaunchedMissiles.ContainsByPredicate([CorrelationMissile, C, A](const FRadarLaunchedMissileStatus& Status)
			{ return Status.MissileComponent == CorrelationMissile && Status.LaunchRadar == C &&
				Status.RelevantRadar == A; }));
	TestEqual(TEXT("selection survives donor fallback"), C->GetSelectedContactID(), StableContactID);
	TestEqual(TEXT("stale local bug is released on donor fallback"), C->GetBuggedTrackID(), -1);
	TestTrue(TEXT("active missile still has original target actor"),
		CorrelationMissile->GetTargetSolution().TargetActor == Target);
	AActor* Neighbor = World->SpawnActor<AActor>();
	USceneComponent* NeighborRoot = NewObject<USceneComponent>(Neighbor);
	Neighbor->SetRootComponent(NeighborRoot);
	NeighborRoot->RegisterComponent();
	Neighbor->SetActorLocation(Target->GetActorLocation() + FVector(100.0f, 0.0f, 0.0f));
	FRadarTrack NeighborRaw = LocalRaw;
	NeighborRaw.TrackedActor = Neighbor;
	NeighborRaw.LastKnownPosition = Neighbor->GetActorLocation();
	FRadarTrack NeighborTrack;
	TestTrue(TEXT("nearby second actor gets its own measured track"),
		C->GetTrackByID(C->CreateTrack(Neighbor, NeighborRaw), NeighborTrack));
	TestNotEqual(TEXT("nearby actor cannot inherit correlated target identity"),
		NeighborTrack.ContactID, StableContactID);
	FMissileTargetSolution WrongActorUpdate = CorrelationMissile->GetTargetSolution();
	WrongActorUpdate.TargetActor = Neighbor;
	WrongActorUpdate.Position = NeighborRaw.LastKnownPosition;
	WrongActorUpdate.MeasurementTimeSeconds = World->GetTimeSeconds();
	CorrelationMissile->ReceiveMidCourseUpdate(WrongActorUpdate);
	TestTrue(TEXT("nearby actor report cannot retarget in-flight missile"),
		CorrelationMissile->GetTargetSolution().TargetActor == Target);
	const FMissileTargetSolution AcceptedUpdate = CorrelationMissile->GetTargetSolution();
	FMissileTargetSolution StaleUpdate = AcceptedUpdate;
	StaleUpdate.SourceParticipantID = 0;
	StaleUpdate.SourceTrackID = LocalTrackID;
	StaleUpdate.MeasurementTimeSeconds = World->GetTimeSeconds() -
		CorrelationMissile->MaxMidCourseMeasurementAgeSeconds - 0.1f;
	CorrelationMissile->ReceiveMidCourseUpdate(StaleUpdate);
	TestEqual(TEXT("stale local report cannot replace current donor provenance"),
		CorrelationMissile->GetTargetSolution().SourceParticipantID, AcceptedUpdate.SourceParticipantID);
	FMissileTargetSolution FutureUpdate = AcceptedUpdate;
	FutureUpdate.SourceParticipantID = 0;
	FutureUpdate.SourceTrackID = LocalTrackID;
	FutureUpdate.MeasurementTimeSeconds = World->GetTimeSeconds() + 1.0f;
	CorrelationMissile->ReceiveMidCourseUpdate(FutureUpdate);
	TestEqual(TEXT("future local report cannot replace current donor provenance"),
		CorrelationMissile->GetTargetSolution().SourceParticipantID, AcceptedUpdate.SourceParticipantID);
	A->SetContinuousWaveIllumination(Target, true);
	AWeapon* PassiveActor = World->SpawnActor<AWeapon>();
	PassiveActor->SetActorLocation(C->GetOwner()->GetActorLocation());
	USemiActiveRadarMissileGuidanceComponent* HandoverPassive =
		NewObject<USemiActiveRadarMissileGuidanceComponent>(PassiveActor);
	PassiveActor->AddInstanceComponent(HandoverPassive);
	HandoverPassive->RegisterComponent();
	HandoverPassive->SetUpdatedComponent(PassiveActor->GetRootComponent());
	HandoverPassive->MotorAcceleration = 0.0f;
	HandoverPassive->ActivateWeapon(true);
	FMissileLaunchConfiguration PassiveLaunch = CorrelatedLaunch;
	PassiveLaunch.Target.MeasurementTimeSeconds = World->GetTimeSeconds() - RemoteTarget.TrackAge;
	TestTrue(TEXT("semi-active weapon launches on donor's verified illumination"),
		HandoverPassive->PrepareLaunch(PassiveLaunch) && HandoverPassive->FireWeapon());
	TestTrue(TEXT("semi-active weapon starts with donor illuminator"),
		HandoverPassive->GetIlluminator() == A);
	C->GetLaunchedRadarMissiles(LaunchedMissiles);
	TestTrue(TEXT("semi-active status names donor as current illuminator"),
		LaunchedMissiles.ContainsByPredicate([HandoverPassive, C, A](const FRadarLaunchedMissileStatus& Status)
			{ return Status.MissileComponent == HandoverPassive && Status.LaunchRadar == C &&
				Status.CurrentExternalRadar == A && Status.RelevantRadar == A; }));
	C->SetRadarMode(ERadarOperatingMode::Search);
	C->CreateTrack(Target, LocalRaw);
	HandoverPassive->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("local sight alone cannot steal semi-active illumination"),
		HandoverPassive->GetIlluminator() == A);
	C->SetContinuousWaveIllumination(Target, true);
	HandoverPassive->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("valid donor stays assigned while supporting"),
		HandoverPassive->GetIlluminator() == A);
	A->SetContinuousWaveIllumination(Target, false);
	HandoverPassive->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("replacement illuminator takes over only after verified local CW"),
		HandoverPassive->GetIlluminator() == C);
	C->GetLaunchedRadarMissiles(LaunchedMissiles);
	TestTrue(TEXT("semi-active status follows verified illuminator transfer"),
		LaunchedMissiles.ContainsByPredicate([HandoverPassive, C](const FRadarLaunchedMissileStatus& Status)
			{ return Status.MissileComponent == HandoverPassive && Status.CurrentExternalRadar == C &&
				Status.RelevantRadar == C; }));
	TestTrue(TEXT("receiver has heartbeat connection"), C->IsDataLinkConnected());
	TestFalse(TEXT("hostile receiver excluded by sender policy"), Hostile->GetLinkedTrackByID(-2, Local));
	OtherNet->GetLinkedTracks(Linked);
	TestTrue(TEXT("other network remains isolated"), Linked.IsEmpty());
	UIFFTransponderComponent* SilentIFF = NewObject<UIFFTransponderComponent>(C->GetOwner());
	C->GetOwner()->AddInstanceComponent(SilentIFF);
	SilentIFF->TeamID = 1;
	SilentIFF->bTransponderActive = false;
	SilentIFF->RegisterComponent();
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.1f);
	C->GetLinkedTracks(Linked);
	TestTrue(TEXT("silent IFF transponder cannot receive friendly-only reports"), Linked.IsEmpty());
	SilentIFF->SetTransponderActive(true);
	for (int32 Index = 0; Index < 15; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	C->GetLinkedTracks(Linked);
	TestFalse(TEXT("active friendly IFF resumes reports"), Linked.IsEmpty());
	B->bRelayDataLinkReports = false;
	for (int32 Index = 0; Index < 60; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	C->GetLinkedTracks(Linked);
	TestTrue(TEXT("report expires without relay"), Linked.IsEmpty());
	TestEqual(TEXT("expired selected report is cleared"), C->GetSelectedLinkedTrackID(), -1);
	B->bRelayDataLinkReports = true;
	AActor* Wall = World->SpawnActor<AActor>();
	UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(WallBox);
	WallBox->SetBoxExtent(FVector(1000.0f, 10000.0f, 10000.0f));
	WallBox->SetCollisionObjectType(ECC_WorldStatic);
	WallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallBox->SetCollisionResponseToAllChannels(ECR_Block);
	WallBox->RegisterComponent();
	Wall->SetActorLocation(FVector(150000.0f, 0.0f, 0.0f));
	for (int32 Index = 0; Index < 40; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	C->GetLinkedTracks(Linked);
	TestTrue(TEXT("terrain or structure blocks relay hop"), Linked.IsEmpty());
	TestFalse(TEXT("blocked receiver desynchronizes"), C->IsDataLinkConnected());
	Wall->Destroy();
	for (int32 Index = 0; Index < 20; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	C->GetLinkedTracks(Linked);
	TestFalse(TEXT("relay resumes after obstruction removed"), Linked.IsEmpty());
	A->SetDataLinkContributionEnabled(false);
	for (int32 Index = 0; Index < 60; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	C->GetLinkedTracks(Linked);
	TestTrue(TEXT("disabled contribution withdraws tracks"), Linked.IsEmpty());
	TestTrue(TEXT("heartbeat remains while contribution is off"), C->IsDataLinkConnected());
	C->SetDataLinkReceptionEnabled(false);
	TestFalse(TEXT("disabled reception clears connection"), C->IsDataLinkConnected());
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
