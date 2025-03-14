// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftRadarComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadarDisplaySelectionTest,
	"AircraftCombat.Radar.DisplaySelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadarDisplaySelectionTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("display selection world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AActor* Platform = World->SpawnActor<AActor>();
	AActor* SourcePlatform = World->SpawnActor<AActor>();
	AActor* LocalTarget = World->SpawnActor<AActor>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	UAircraftRadarComponent* Radar = NewObject<UAircraftRadarComponent>(Platform);
	Platform->AddInstanceComponent(Radar);
	Radar->RegisterComponent();
	UAircraftRadarComponent* Source = NewObject<UAircraftRadarComponent>(SourcePlatform);
	SourcePlatform->AddInstanceComponent(Source);
	Source->RegisterComponent();
	Radar->GrantRadarAccess(Controller);
	Radar->SetActiveRadarOperator(Controller);

	FRadarTrack Local;
	Local.LastKnownPosition = FVector(1000000.0f, 0.0f, 0.0f);
	Local.Range = 1000000.0f;
	const int32 LocalID = Radar->CreateTrack(LocalTarget, Local);
	TestTrue(TEXT("local track created"), LocalID > 0);

	Radar->bEnableDataLink = true;
	Source->bEnableDataLink = true;
	Radar->SetDataLinkNetworkID(TEXT("SelectionTest"));
	Source->SetDataLinkNetworkID(TEXT("SelectionTest"));
	Source->SetDataLinkParticipantID(9);
	FRadarTrack Report;
	Report.TrackID = 42;
	Report.LastKnownPosition = FVector(1000000.0f, 200000.0f, 0.0f);
	Radar->ReceiveDataLinkReport(Report, Source, 1.0f, 1.0f);
	TArray<FRadarTrack> Linked;
	Radar->GetLinkedTracks(Linked);
	TestEqual(TEXT("linked contact available"), Linked.Num(), 1);
	const int32 LinkedID = Linked.IsEmpty() ? -1 : Linked[0].TrackID;
	TArray<FRadarTrack> DisplayTracks;
	Radar->GetDisplayTracks(DisplayTracks);

	for (ERadarDisplayGeometry Geometry : {ERadarDisplayGeometry::BScope, ERadarDisplayGeometry::PPI})
	{
		Radar->RequestSetDisplayGeometry(Geometry, Controller);
		const FVector2D Offset = Geometry == ERadarDisplayGeometry::BScope
			? FVector2D(0.0f, -27.0f) : FVector2D(2.0f, 0.0f);
		Radar->RequestSetDisplayWindow(4.0f, Offset, Controller);
		for (int32 ExpectedID : {LocalID, LinkedID})
		{
			FVector2D Symbol;
			const FString Label = FString::Printf(TEXT("%s track %d"),
				Geometry == ERadarDisplayGeometry::PPI ? TEXT("PPI") : TEXT("B-scope"), ExpectedID);
			const FRadarTrack* DisplayTrack = DisplayTracks.FindByPredicate(
				[ExpectedID](const FRadarTrack& Track) { return Track.TrackID == ExpectedID; });
			if (!TestNotNull(*FString::Printf(TEXT("%s display track exists"), *Label), DisplayTrack)) continue;
			TestTrue(*FString::Printf(TEXT("%s symbol is visible"), *Label),
				Radar->ProjectWorldToDisplay(DisplayTrack->LastKnownPosition, FVector2D(512.0f, 512.0f), Symbol));
			Radar->RequestSetCursorFromDisplayPosition(Symbol, FVector2D(512.0f, 512.0f), Controller);
			FVector2D Cursor;
			TestTrue(*FString::Printf(TEXT("%s cursor is visible"), *Label),
				Radar->ProjectWorldToDisplay(Radar->GetTDCCursorState().WorldLocation,
					FVector2D(512.0f, 512.0f), Cursor));
			TestTrue(*FString::Printf(TEXT("%s cursor overlaps symbol"), *Label),
				Cursor.Equals(Symbol, 0.5f));
			int32 SelectedID = -1;
			TestTrue(*FString::Printf(TEXT("%s designation succeeds"), *Label),
				Radar->ExecuteOperatorCommand(ERadarCommandType::DesignateCursor, 0, 0.0f, 0.0f,
					FVector::ZeroVector, FRadarCursorState(), Geometry, SelectedID,
					Radar->GetDisplayViewRevision()));
			TestEqual(*FString::Printf(TEXT("%s designation picks displayed contact"), *Label),
				SelectedID, ExpectedID);
		}
	}

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
