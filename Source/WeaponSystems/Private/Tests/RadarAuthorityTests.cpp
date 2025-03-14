// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftRadarComponent.h"
#include "RadarOperatorLink.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadarAuthorityTest,
	"AircraftCombat.Radar.AuthorityAndCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadarAuthorityTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("radar authority world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AActor* Platform = World->SpawnActor<AActor>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	APlayerController* OtherController = World->SpawnActor<APlayerController>();
	UAircraftRadarComponent* Radar = NewObject<UAircraftRadarComponent>(Platform);
	Platform->AddInstanceComponent(Radar);
	Radar->RegisterComponent();
	TestNotNull(TEXT("standalone platform radar"), Radar);
	TestTrue(TEXT("standalone radar has authority"), Platform->HasAuthority());

	// UI requests must carry a controller even on a listen server; direct server methods remain available.
	Radar->RequestSetRadarMode(ERadarOperatingMode::Standby, nullptr);
	TestEqual(TEXT("anonymous UI request rejected"), Radar->GetRadarMode(), ERadarOperatingMode::Search);
	TestTrue(TEXT("crew controller can be granted"), Radar->GrantRadarAccess(Controller));
	TestTrue(TEXT("crew controller can become active"), Radar->SetActiveRadarOperator(Controller));
	Radar->RequestSetRadarMode(ERadarOperatingMode::Standby, Controller);
	TestEqual(TEXT("active local operator changes mode"), Radar->GetRadarMode(), ERadarOperatingMode::Standby);
	TestTrue(TEXT("second crew controller can be granted"), Radar->GrantRadarAccess(OtherController));
	Radar->RequestSetRadarMode(ERadarOperatingMode::Search, OtherController);
	TestEqual(TEXT("inactive crew cannot control radar"), Radar->GetRadarMode(), ERadarOperatingMode::Standby);
	TestTrue(TEXT("control transfers to second crew controller"), Radar->SetActiveRadarOperator(OtherController));
	Radar->RequestSetRadarMode(ERadarOperatingMode::Search, OtherController);
	TestEqual(TEXT("new active crew controls radar"), Radar->GetRadarMode(), ERadarOperatingMode::Search);
	Radar->RevokeRadarAccess(Controller);
	Radar->RequestSetRadarMode(ERadarOperatingMode::Standby, Controller);
	TestEqual(TEXT("revoked operator request rejected"), Radar->GetRadarMode(), ERadarOperatingMode::Search);
	Radar->SetRadarMode(ERadarOperatingMode::Standby);
	TestEqual(TEXT("server script still has direct authority"), Radar->GetRadarMode(), ERadarOperatingMode::Standby);

	FRadarCursorState Cursor;
	Cursor.AzimuthDegrees = 120.0f;
	Cursor.SlantRangeCm = 100000.0f;
	int32 TrackID = -1;
	TestFalse(TEXT("out-of-sector exact cursor request rejected"), Radar->ExecuteOperatorCommand(
		ERadarCommandType::SetCursor, 0, 0.0f, 0.0f, FVector::ZeroVector,
		Cursor, ERadarDisplayGeometry::BScope, TrackID));
	Cursor.AzimuthDegrees = 20.0f;
	Cursor.SlantRangeCm = 100000000.0f;
	TestFalse(TEXT("out-of-range cursor request rejected"), Radar->ExecuteOperatorCommand(
		ERadarCommandType::SetCursor, 0, 0.0f, 0.0f, FVector::ZeroVector,
		Cursor, ERadarDisplayGeometry::BScope, TrackID));
	Cursor.SlantRangeCm = 100000.0f;
	TestTrue(TEXT("in-range cursor request accepted"), Radar->ExecuteOperatorCommand(
		ERadarCommandType::SetCursor, 0, 0.0f, 0.0f, FVector::ZeroVector,
		Cursor, ERadarDisplayGeometry::BScope, TrackID));
	TestTrue(TEXT("cursor keeps exact azimuth"),
		FMath::IsNearlyEqual(Radar->GetTDCCursorState().AzimuthDegrees, 20.0f));
	Radar->RequestSetDisplayGeometry(ERadarDisplayGeometry::PPI, OtherController);
	TestEqual(TEXT("active crew selects shared PPI view"), Radar->GetDisplayView().ActiveGeometry,
		ERadarDisplayGeometry::PPI);
	const FVector VisualCheckPoint(2000000.0f, 0.0f, 0.0f);
	const FVector2D VisualCheckSize(512.0f, 512.0f);
	FVector2D FullViewPixel, ZoomedPixel, PannedPixel, ClientPixel;
	TestTrue(TEXT("full PPI view projects the check point"),
		Radar->ProjectWorldToDisplay(VisualCheckPoint, VisualCheckSize, FullViewPixel));
	Radar->RequestSetDisplayWindow(2.0f, FVector2D::ZeroVector, OtherController);
	TestTrue(TEXT("zoomed PPI view projects the check point"),
		Radar->ProjectWorldToDisplay(VisualCheckPoint, VisualCheckSize, ZoomedPixel));
	TestTrue(TEXT("active PPI request visibly zooms the symbol"),
		FVector2D::Distance(FullViewPixel, ZoomedPixel) > 30.0f);
	Radar->RequestSetDisplayWindow(2.0f, FVector2D(10.0f, 0.0f), OtherController);
	TestTrue(TEXT("panned PPI view projects the check point"),
		Radar->ProjectWorldToDisplay(VisualCheckPoint, VisualCheckSize, PannedPixel));
	TestTrue(TEXT("ten-kilometer PPI pan visibly moves the symbol"),
		FVector2D::Distance(ZoomedPixel, PannedPixel) > 30.0f);
	FRadarOperatorSnapshot ClientSnapshot;
	ClientSnapshot.Revision = 1;
	ClientSnapshot.DisplayView = Radar->GetDisplayView();
	ClientSnapshot.DisplayViewRevision = Radar->GetDisplayViewRevision();
	ClientSnapshot.DisplayRangeCm = Radar->CurrentDisplayRange;
	ClientSnapshot.AzimuthScanWidth = Radar->AzimuthScanWidth;
	UAircraftRadarComponent* ClientRadar = NewObject<UAircraftRadarComponent>();
	ClientRadar->ApplyOperatorSnapshot(ClientSnapshot);
	TestTrue(TEXT("owner snapshot projects the panned symbol"),
		ClientRadar->ProjectWorldToDisplay(VisualCheckPoint, VisualCheckSize, ClientPixel));
	TestTrue(TEXT("owner snapshot uses server PPI zoom and pan"),
		ClientPixel.Equals(PannedPixel, 0.1f));
	const FVector2D SavedPPIPixel = PannedPixel;
	Radar->RequestSetDisplayGeometry(ERadarDisplayGeometry::BScope, OtherController);
	const FVector BScopeCheckPoint(4000000.0f, 0.0f, 0.0f);
	TestTrue(TEXT("full B-scope projects the check point"),
		Radar->ProjectWorldToDisplay(BScopeCheckPoint, VisualCheckSize, FullViewPixel));
	Radar->RequestSetDisplayWindow(2.0f, FVector2D::ZeroVector, OtherController);
	TestTrue(TEXT("zoomed B-scope projects the check point"),
		Radar->ProjectWorldToDisplay(BScopeCheckPoint, VisualCheckSize, ZoomedPixel));
	TestTrue(TEXT("active B-scope request visibly zooms the symbol"),
		FVector2D::Distance(FullViewPixel, ZoomedPixel) > 10.0f);
	Radar->RequestSetDisplayWindow(2.0f, FVector2D(10.0f, 10.0f), OtherController);
	TestTrue(TEXT("panned B-scope projects the check point"),
		Radar->ProjectWorldToDisplay(BScopeCheckPoint, VisualCheckSize, PannedPixel));
	TestTrue(TEXT("B-scope degree and kilometer pan visibly moves the symbol"),
		FVector2D::Distance(ZoomedPixel, PannedPixel) > 30.0f);
	ClientSnapshot.Revision = 2;
	ClientSnapshot.DisplayView = Radar->GetDisplayView();
	ClientSnapshot.DisplayViewRevision = Radar->GetDisplayViewRevision();
	ClientRadar->ApplyOperatorSnapshot(ClientSnapshot);
	TestTrue(TEXT("owner snapshot projects the B-scope symbol"),
		ClientRadar->ProjectWorldToDisplay(BScopeCheckPoint, VisualCheckSize, ClientPixel));
	TestTrue(TEXT("owner snapshot uses server B-scope zoom and pan"),
		ClientPixel.Equals(PannedPixel, 0.1f));
	Radar->RequestSetDisplayGeometry(ERadarDisplayGeometry::PPI, OtherController);
	TestTrue(TEXT("switching back restores the PPI window"),
		Radar->ProjectWorldToDisplay(VisualCheckPoint, VisualCheckSize, ClientPixel) &&
		ClientPixel.Equals(SavedPPIPixel, 0.1f));
	Radar->RequestSetDisplayWindow(3.0f,
		FVector2D(1.0f, -1.0f), OtherController);
	TestTrue(TEXT("shared view stores operator zoom"),
		FMath::IsNearlyEqual(Radar->GetDisplayView().PPI.ZoomFactor, 3.0f));
	TestTrue(TEXT("one-kilometer offset is not enlarged or discarded"),
		Radar->GetDisplayView().PPI.ViewCenterOffset.Equals(FVector2D(1.0f, -1.0f)));
	Radar->RequestSetDisplayWindow(5.0f,
		FVector2D::ZeroVector, Controller);
	TestTrue(TEXT("inactive crew cannot change shared view"),
		FMath::IsNearlyEqual(Radar->GetDisplayView().PPI.ZoomFactor, 3.0f));
	const int32 OldViewRevision = Radar->GetDisplayViewRevision() - 1;
	TestFalse(TEXT("stale view cannot place cursor"), Radar->ExecuteOperatorCommand(
		ERadarCommandType::SetCursor, 0, 0.0f, 0.0f, FVector::ZeroVector,
		Cursor, ERadarDisplayGeometry::PPI, TrackID, OldViewRevision));
	TestFalse(TEXT("stale view cannot designate contact"), Radar->ExecuteOperatorCommand(
		ERadarCommandType::DesignateCursor, 0, 0.0f, 0.0f, FVector::ZeroVector,
		Cursor, ERadarDisplayGeometry::PPI, TrackID, OldViewRevision));
	FVector2D CursorPixel;
	TestTrue(TEXT("PPI cursor image projects through shared view"),
		Radar->ProjectWorldToDisplay(Radar->GetTDCCursorState().WorldLocation,
			FVector2D(512.0f, 512.0f), CursorPixel));
	Radar->RequestSetCursorFromDisplayPosition(CursorPixel, FVector2D(512.0f, 512.0f), OtherController);
	FVector2D ReprojectedCursor;
	TestTrue(TEXT("placed cursor remains visible"),
		Radar->ProjectWorldToDisplay(Radar->GetTDCCursorState().WorldLocation,
			FVector2D(512.0f, 512.0f), ReprojectedCursor));
	TestTrue(TEXT("cursor image and physical cursor remain aligned"),
		ReprojectedCursor.Equals(CursorPixel, 0.5f));
	Platform->SetActorRotation(FRotator(20.0f, 30.0f, 25.0f));
	FVector2D TiltedSymbol;
	TestTrue(TEXT("pitched and banked PPI projects a map point"),
		Radar->ProjectWorldToDisplay(FVector(150000.0f, 200000.0f, 0.0f),
			FVector2D(600.0f, 400.0f), TiltedSymbol));
	Radar->RequestSetCursorFromDisplayPosition(TiltedSymbol, FVector2D(600.0f, 400.0f), OtherController);
	TestTrue(TEXT("pitched and banked cursor image remains visible"),
		Radar->ProjectWorldToDisplay(Radar->GetTDCCursorState().WorldLocation,
			FVector2D(600.0f, 400.0f), ReprojectedCursor));
	TestTrue(TEXT("pitched and banked physical cursor aligns with the PPI symbol"),
		ReprojectedCursor.Equals(TiltedSymbol, 0.5f));
	Platform->SetActorRotation(FRotator::ZeroRotator);
	const FVector2D CanvasSize(512.0f, 512.0f);
	Radar->RequestSetDisplayWindow(1.0f,
		FVector2D::ZeroVector, OtherController);
	FVector2D StartPixel;
	TestTrue(TEXT("center-aligned PPI origin has zero translation"),
		Radar->ProjectWorldToDisplay(Radar->GetRadarLocation(), CanvasSize, StartPixel) &&
			StartPixel.IsNearlyZero());
	TestTrue(TEXT("unzoomed cursor start projects"),
		Radar->ProjectWorldToDisplay(FVector(300000.0f, 0.0f, 0.0f), CanvasSize, StartPixel));
	Radar->RequestSetCursorFromDisplayPosition(StartPixel, CanvasSize, OtherController);
	World->Tick(LEVELTICK_All, 0.1f);
	Radar->MoveTDCCursor(1.0f, 0.0f);
	FVector2D MovedPixel;
	TestTrue(TEXT("unzoomed cursor move projects"),
		Radar->ProjectWorldToDisplay(Radar->GetTDCCursorState().WorldLocation,
			CanvasSize, MovedPixel));
	const float UnzoomedScreenMovement = MovedPixel.X - StartPixel.X;
	Radar->RequestSetDisplayWindow(2.0f,
		FVector2D::ZeroVector, OtherController);
	TestTrue(TEXT("zoomed cursor start projects"),
		Radar->ProjectWorldToDisplay(FVector(300000.0f, 0.0f, 0.0f), CanvasSize, StartPixel));
	Radar->RequestSetCursorFromDisplayPosition(StartPixel, CanvasSize, OtherController);
	World->Tick(LEVELTICK_All, 0.1f);
	Radar->MoveTDCCursor(1.0f, 0.0f);
	TestTrue(TEXT("zoomed cursor move projects"),
		Radar->ProjectWorldToDisplay(Radar->GetTDCCursorState().WorldLocation,
			CanvasSize, MovedPixel));
	TestTrue(TEXT("zoom keeps cursor speed constant in MFD pixels"),
		FMath::IsNearlyEqual(MovedPixel.X - StartPixel.X, UnzoomedScreenMovement, 1.0f));
	Radar->bEnableTargetCursor = false;
	TestFalse(TEXT("cursor feature reports disabled"), Radar->IsTargetCursorEnabled());
	FVector CursorWorld = FVector(1.0f);
	TestFalse(TEXT("disabled cursor has no world point"), Radar->TryGetTDCCursorWorldLocation(CursorWorld));
	TestTrue(TEXT("disabled cursor world point is inert"), CursorWorld.IsNearlyZero());
	TestFalse(TEXT("disabled cursor rejects exact placement"), Radar->ExecuteOperatorCommand(
		ERadarCommandType::SetCursor, 0, 0.0f, 0.0f, FVector::ZeroVector,
		Cursor, ERadarDisplayGeometry::BScope, TrackID));
	TestFalse(TEXT("disabled cursor rejects designation"), Radar->ExecuteOperatorCommand(
		ERadarCommandType::DesignateCursor, 0, 0.0f, 0.0f, FVector::ZeroVector,
		Cursor, ERadarDisplayGeometry::BScope, TrackID));
	TestEqual(TEXT("disabled cursor request is a no-op"),
		Radar->RequestDesignateCursor(OtherController), INDEX_NONE);

	USceneComponent* Reference = NewObject<USceneComponent>(Platform, TEXT("RadarReference"));
	Platform->SetRootComponent(Reference);
	Reference->RegisterComponent();
	Reference->SetWorldRotation(FRotator(0.0f, 30.0f, 0.0f));
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Platform);
	UStaticMeshSocket* SourceSocket = NewObject<UStaticMeshSocket>(Mesh);
	SourceSocket->SocketName = TEXT("DishSocket");
	Mesh->Sockets.Add(SourceSocket);
	UStaticMeshComponent* Dish = NewObject<UStaticMeshComponent>(Platform, TEXT("Dish"));
	Dish->SetMobility(EComponentMobility::Movable);
	Dish->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dish->SetupAttachment(Reference);
	Dish->SetStaticMesh(Mesh);
	Dish->RegisterComponent();
	Radar->ScanDrive = ERadarScanDrive::SocketDriven;
	Radar->SetRadarSocketName(TEXT("DishSocket"));
	Radar->RadarReferenceComponentName = Reference->GetFName();
	FVector SourceLocation, ReferenceLocation;
	FRotator SourceRotation, ReferenceRotation;
	Radar->GetRadarSourceTransform(SourceLocation, SourceRotation);
	Radar->GetRadarReferenceTransform(ReferenceLocation, ReferenceRotation);
	TestTrue(TEXT("socket initially points with stable platform"),
		FMath::IsNearlyEqual(SourceRotation.Yaw, 30.0f, 0.1f));
	Dish->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
	Radar->GetRadarSourceTransform(SourceLocation, SourceRotation);
	Radar->GetRadarReferenceTransform(ReferenceLocation, ReferenceRotation);
	TestTrue(TEXT("rotating dish changes actual source axis"),
		FMath::IsNearlyEqual(SourceRotation.Yaw, 120.0f, 0.1f));
	TestTrue(TEXT("rotating dish leaves reference bearing stable"),
		FMath::IsNearlyEqual(ReferenceRotation.Yaw, 30.0f, 0.1f));

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
