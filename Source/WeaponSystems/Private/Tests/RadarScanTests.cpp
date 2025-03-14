// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftRadarComponent.h"
#include "AircraftCombatSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadarScanTest,
	"AircraftCombat.Radar.ScanReturns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadarScanTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("radar scan world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AActor* Platform = World->SpawnActor<AActor>();
	USceneComponent* PlatformRoot = NewObject<USceneComponent>(Platform);
	Platform->SetRootComponent(PlatformRoot);
	PlatformRoot->RegisterComponent();
	UAircraftRadarComponent* Radar = NewObject<UAircraftRadarComponent>(Platform);
	Platform->AddInstanceComponent(Radar);
	Radar->ScanDrive = ERadarScanDrive::AESA;
	Radar->AESABeamsPerSample = 16;
	Radar->AzimuthScanWidth = 360.0f;
	Radar->bOmnidirectionalTracking = true;
	Radar->ElevationScanHeight = 30.0f;
	Radar->ElevationBars = 1;
	Radar->BeamAzimuthWidth = 45.0f;
	Radar->BeamElevationWidth = 45.0f;
	Radar->MaxDetectionRange = 1000000.0f;
	Radar->MinDetectionRange = 0.0f;
	Radar->ScanSampleInterval = 0.05f;
	Radar->CandidateDiscoveryInterval = 0.1f;
	Radar->bEnablePhysicsCandidateDiscovery = false;
	Radar->bEnableTargetRCSTagParsing = false;
	Radar->bEnableIFF = false;
	Radar->bEnableTerrainMasking = false;
	Radar->RegisterComponent();

	AActor* Target = World->SpawnActor<AActor>();
	UBoxComponent* TargetBox = NewObject<UBoxComponent>(Target);
	Target->SetRootComponent(TargetBox);
	TargetBox->SetBoxExtent(FVector(1000.0f));
	TargetBox->SetCollisionObjectType(ECC_WorldDynamic);
	TargetBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TargetBox->SetCollisionResponseToAllChannels(ECR_Overlap);
	TargetBox->RegisterComponent();
	Target->SetActorLocation(FVector(100000.0f, 0.0f, 0.0f));
	Target->Tags.Add(TEXT("RCS=0.01"));
	UAircraftCombatSubsystem* Registry = World->GetSubsystem<UAircraftCombatSubsystem>();
	TestNotNull(TEXT("combat registry available"), Registry);
	if (Registry) Registry->RegisterCombatActor(Target);
	World->BeginPlay();
	Platform->DispatchBeginPlay();
	TestTrue(TEXT("radar platform has begun play"), Platform->HasActorBegunPlay());
	TestTrue(TEXT("radar component ticking"), Radar->IsComponentTickEnabled());
	if (Registry)
	{
		TArray<AActor*> Registered;
		Registry->GetCombatActorsInVolume(FVector::ZeroVector, Radar->MaxDetectionRange, Registered);
		TestTrue(TEXT("target present in candidate registry"), Registered.Contains(Target));
	}
	for (int32 TickIndex = 0; TickIndex < 5; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	FRadarTrack Track;
	const bool bDetected = Radar->GetTrackByActor(Target, Track);
	AddInfo(FString::Printf(TEXT("Scan azimuth %.1f, track count %d, mode %d"),
		Radar->GetCurrentScanAzimuth(), Radar->GetAllTracks().Num(), static_cast<int32>(Radar->GetRadarMode())));
	TestTrue(TEXT("AESA detects an untagged target without mission management"), bDetected);
	if (bDetected)
	{
		TestFalse(TEXT("RCS tag parsing can be isolated"), Track.bHasRCSTag);
		TestTrue(TEXT("disabled RCS parsing uses configured default"),
			FMath::IsNearlyEqual(Track.RCS, Radar->DefaultTargetRCS));
		TestEqual(TEXT("disabled IFF remains unknown"), Track.IFFResult, ERadarIFFResult::Unknown);
		TestTrue(TEXT("initial radar position is measured"),
			FVector::Dist(Track.LastKnownPosition, Target->GetActorLocation()) < 10.0f);
		Target->SetActorLocation(FVector(120000.0f, 0.0f, 0.0f));
		for (int32 TickIndex = 0; TickIndex < 3; ++TickIndex)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.1f);
		}
		TestTrue(TEXT("new return updates measured position"), Radar->GetTrackByActor(Target, Track) &&
			FVector::Dist(Track.LastKnownPosition, Target->GetActorLocation()) < 10.0f);
		TestTrue(TEXT("successive returns estimate motion"), Track.EstimatedVelocity.X > 0.0f);
	}
	AActor* RearTarget = World->SpawnActor<AActor>();
	UBoxComponent* RearBox = NewObject<UBoxComponent>(RearTarget);
	RearTarget->SetRootComponent(RearBox);
	RearBox->SetBoxExtent(FVector(1000.0f));
	RearBox->SetCollisionObjectType(ECC_WorldDynamic);
	RearBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RearBox->SetCollisionResponseToAllChannels(ECR_Overlap);
	RearBox->RegisterComponent();
	RearTarget->SetActorLocation(FVector(-100000.0f, 0.0f, 0.0f));
	if (Registry) Registry->RegisterCombatActor(RearTarget);
	for (int32 TickIndex = 0; TickIndex < 3; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	FRadarTrack RearTrack;
	TestTrue(TEXT("360-degree AESA scan reaches rear sector"), Radar->GetTrackByActor(RearTarget, RearTrack));
	Radar->SetRadarMode(ERadarOperatingMode::Standby);
	Radar->ScanDrive = ERadarScanDrive::PESA;
	Radar->SetScanVolume(120.0f, 30.0f, 1);
	Target->SetActorLocation(FVector(125000.0f, 0.0f, 0.0f));
	Radar->SetRadarMode(ERadarOperatingMode::Search);
	for (int32 TickIndex = 0; TickIndex < 5; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	TestTrue(TEXT("PESA single-beam visit measures moved forward target"),
		Radar->GetTrackByActor(Target, Track) &&
		FVector::Dist(Track.LastKnownPosition, Target->GetActorLocation()) < 10.0f);
	int32 ProgressBar = -1;
	float BarLevel = -1.0f;
	float SweepLevel = -1.0f;
	bool bProgressMovingRight = false;
	Radar->GetScanProgress(ProgressBar, BarLevel, SweepLevel, bProgressMovingRight);
	TestTrue(TEXT("electronic scan progress preserves its current sample"),
		ProgressBar == Radar->GetCurrentScanBar() && BarLevel >= 0.0f && BarLevel <= 1.0f &&
		SweepLevel >= 0.0f && SweepLevel <= 1.0f && bProgressMovingRight);

	AActor* Wall = World->SpawnActor<AActor>();
	UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(WallBox);
	WallBox->SetBoxExtent(FVector(1000.0f, 10000.0f, 10000.0f));
	WallBox->SetCollisionObjectType(ECC_WorldStatic);
	WallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallBox->SetCollisionResponseToAllChannels(ECR_Block);
	WallBox->RegisterComponent();
	Wall->SetActorLocation(FVector(60000.0f, 0.0f, 0.0f));
	Radar->bEnableTerrainMasking = true;
	Radar->TrackDropTimeout = 1.0f;
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.1f);
	const FVector LastReturn = Track.LastKnownPosition;
	Target->SetActorLocation(FVector(140000.0f, 0.0f, 0.0f));
	for (int32 TickIndex = 0; TickIndex < 3; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	TestTrue(TEXT("occlusion preserves last measured position"), Radar->GetTrackByActor(Target, Track) &&
		FVector::Dist(Track.LastKnownPosition, LastReturn) < 10.0f && Track.TrackAge > 0.0f);
	for (int32 TickIndex = 0; TickIndex < 15; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	TestFalse(TEXT("missing returns eventually prune track"), Radar->GetTrackByActor(Target, Track));
	Wall->Destroy();
	Radar->bEnableTerrainMasking = false;
	Radar->ScanDrive = ERadarScanDrive::VirtualMechanical;
	Radar->ScanRateDegreesPerSecond = 720.0f;
	Radar->SetScanVolume(120.0f, 30.0f, 1);
	for (int32 TickIndex = 0; TickIndex < 8; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	TestTrue(TEXT("static mount virtual scan reacquires target"), Radar->GetTrackByActor(Target, Track));

	UStaticMesh* DishMesh = NewObject<UStaticMesh>(Platform);
	UStaticMeshSocket* SourceSocket = NewObject<UStaticMeshSocket>(DishMesh);
	SourceSocket->SocketName = TEXT("RadarDishSocket");
	DishMesh->Sockets.Add(SourceSocket);
	UStaticMeshComponent* Dish = NewObject<UStaticMeshComponent>(Platform, TEXT("RadarDish"));
	Dish->SetMobility(EComponentMobility::Movable);
	Dish->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dish->SetupAttachment(PlatformRoot);
	Dish->SetStaticMesh(DishMesh);
	Dish->RegisterComponent();
	Dish->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	Radar->ScanDrive = ERadarScanDrive::SocketDriven;
	Radar->SetRadarSocketName(TEXT("RadarDishSocket"));
	Radar->SetRadarMode(ERadarOperatingMode::Standby);
	Radar->SetRadarMode(ERadarOperatingMode::Search);
	for (int32 TickIndex = 0; TickIndex < 4; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	TestTrue(TEXT("socket-driven scan follows rear-facing dish"),
		Radar->GetTrackByActor(RearTarget, RearTrack));
	TestFalse(TEXT("socket-driven scan does not add a virtual forward beam"),
		Radar->GetTrackByActor(Target, Track));
	Dish->SetRelativeRotation(FRotator::ZeroRotator);
	for (int32 TickIndex = 0; TickIndex < 4; ++TickIndex)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.1f);
	}
	TestTrue(TEXT("rotating socket forward acquires forward target"), Radar->GetTrackByActor(Target, Track));

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
