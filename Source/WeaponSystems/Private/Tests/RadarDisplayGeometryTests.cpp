// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "RadarDisplayGeometry.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadarDisplayGeometryTest,
	"AircraftCombat.Radar.DisplayGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadarDisplayGeometryTest::RunTest(const FString& Parameters)
{
	FRadarDisplayProjection Projection;
	Projection.RadarOrigin = FVector(120000.0, -35000.0, 18000.0);
	Projection.ReferenceRotation = FRotator(0.0, 35.0, 0.0);
	Projection.WidgetTopLeft = FVector2D(20.0, 40.0);
	Projection.WidgetSize = FVector2D(600.0, 400.0);
	Projection.DisplayRangeCm = 100000.0f;
	Projection.ScanCenterAzimuth = 170.0f;
	Projection.AzimuthWidth = 80.0f;
	Projection.CursorElevation = 7.0f;

	FRadarCursorState Cursor;
	Cursor.AzimuthDegrees = -175.0f; // 15 degrees right of a 170-degree center.
	Cursor.ElevationDegrees = Projection.CursorElevation;
	Cursor.SlantRangeCm = 80000.0f;
	const FVector TrackWorld = FRadarDisplayGeometryMath::CursorToWorld(Cursor, Projection);
	FVector2D TrackPixel;
	TestTrue(TEXT("wrapped B-scope bearing is visible"),
		FRadarDisplayGeometryMath::Project(TrackWorld, Projection, TrackPixel));
	TestTrue(TEXT("wrapped B-scope X"), FMath::IsNearlyEqual(TrackPixel.X, 432.5f, 0.1f));
	TestTrue(TEXT("B-scope range Y"), FMath::IsNearlyEqual(TrackPixel.Y, 120.0f, 0.1f));
	FVector RecoveredWorld;
	TestTrue(TEXT("B-scope inverse accepts visible pixel"),
		FRadarDisplayGeometryMath::Unproject(TrackPixel, Projection, RecoveredWorld));
	TestTrue(TEXT("B-scope cursor and measured track align"),
		FVector::Dist(RecoveredWorld, TrackWorld) < 5.0f);
	TestFalse(TEXT("B-scope rejects widget coordinate outside bounds"),
		FRadarDisplayGeometryMath::Unproject(FVector2D(19.0, 200.0), Projection, RecoveredWorld));
	TestTrue(TEXT("B-scope bottom edge keeps its azimuth"),
		FRadarDisplayGeometryMath::Unproject(FVector2D(432.5, 440.0), Projection, RecoveredWorld));
	const FRotator BottomLocalLook = Projection.ReferenceRotation.UnrotateVector(
		RecoveredWorld - Projection.RadarOrigin).Rotation();
	TestTrue(TEXT("zero-range singularity uses one-centimeter cursor direction"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(-175.0f, BottomLocalLook.Yaw)) < 0.1f);
	const FVector OutOfRangeWorld = Projection.RadarOrigin +
		Projection.ReferenceRotation.RotateVector(FRotator(7.0, -175.0, 0.0).Vector() * 110000.0);
	TestFalse(TEXT("B-scope reports out-of-range symbols"),
		FRadarDisplayGeometryMath::Project(OutOfRangeWorld, Projection, TrackPixel));

	Projection.Geometry = ERadarDisplayGeometry::PPI;
	Projection.bHeadingUp = true;
	const FVector PPIWorld = Projection.RadarOrigin + FVector(20000.0, 30000.0, 12000.0);
	TestTrue(TEXT("PPI projects point inside circle"),
		FRadarDisplayGeometryMath::Project(PPIWorld, Projection, TrackPixel));
	TestTrue(TEXT("PPI inverse accepts point"),
		FRadarDisplayGeometryMath::Unproject(TrackPixel, Projection, RecoveredWorld));
	TestTrue(TEXT("PPI planar round trip"),
		FVector::Dist2D(PPIWorld, RecoveredWorld) < 2.0f);
	const FVector2D HeadingUpPixel = TrackPixel;
	Projection.bHeadingUp = false;
	TestTrue(TEXT("north-up PPI projects point"),
		FRadarDisplayGeometryMath::Project(PPIWorld, Projection, TrackPixel));
	TestTrue(TEXT("changing heading orientation moves symbol"),
		FVector2D::Distance(HeadingUpPixel, TrackPixel) > 10.0f);
	TestFalse(TEXT("PPI rejects square corner outside circle"),
		FRadarDisplayGeometryMath::Unproject(Projection.WidgetTopLeft, Projection, RecoveredWorld));
	TestTrue(TEXT("PPI center accepts exact cursor input"),
		FRadarDisplayGeometryMath::Unproject(Projection.WidgetTopLeft + 0.5f * Projection.WidgetSize,
			Projection, RecoveredWorld));
	TestTrue(TEXT("PPI center retains a defined polar direction"),
		FVector::Dist(RecoveredWorld, Projection.RadarOrigin) >= 0.99f);

	Projection.bHeadingUp = true;
	const FVector NearEdgeTrack = Projection.RadarOrigin + FVector(95000.0, 0.0, 0.0);
	const FVector NearEdgeCursor = Projection.RadarOrigin + FVector(96000.0, 1000.0, 0.0);
	FVector2D EdgeTrackPixel, EdgeCursorPixel;
	TestTrue(TEXT("track near PPI edge remains visible"),
		FRadarDisplayGeometryMath::Project(NearEdgeTrack, Projection, EdgeTrackPixel));
	TestTrue(TEXT("cursor near PPI edge remains visible"),
		FRadarDisplayGeometryMath::Project(NearEdgeCursor, Projection, EdgeCursorPixel));
	const FVector2D NormalizedDelta = (EdgeTrackPixel - EdgeCursorPixel) / Projection.WidgetSize;
	TestTrue(TEXT("edge designation uses same projection for track and cursor"),
		NormalizedDelta.Size() < 0.04f);

	// The MFD is a movable window over the full map; the radar stays fixed.
	Projection.Geometry = ERadarDisplayGeometry::BScope;
	Projection.Window.ZoomFactor = 2.0f;
	Projection.Window.ViewCenterOffset = FVector2D(15.0f, 0.3f);
	TestTrue(TEXT("zoomed and panned B-scope contact is visible"),
		FRadarDisplayGeometryMath::Project(TrackWorld, Projection, TrackPixel));
	TestTrue(TEXT("zoomed B-scope inverse accepts symbol position"),
		FRadarDisplayGeometryMath::Unproject(TrackPixel, Projection, RecoveredWorld));
	TestTrue(TEXT("zoomed B-scope cursor aligns with contact"),
		FVector::Dist(RecoveredWorld, TrackWorld) < 5.0f);
	const FVector2D BScopeNormalized = (TrackPixel - Projection.WidgetTopLeft) / Projection.WidgetSize;
	Projection.WidgetSize = FVector2D(300.0f, 700.0f);
	TestTrue(TEXT("resized B-scope still shows contact"),
		FRadarDisplayGeometryMath::Project(TrackWorld, Projection, TrackPixel));
	TestTrue(TEXT("resizing preserves normalized symbol position"),
		((TrackPixel - Projection.WidgetTopLeft) / Projection.WidgetSize).Equals(BScopeNormalized, 0.001f));
	TestFalse(TEXT("panned B-scope hides radar origin outside window"),
		FRadarDisplayGeometryMath::Project(Projection.RadarOrigin, Projection, TrackPixel));

	Projection.Geometry = ERadarDisplayGeometry::PPI;
	Projection.WidgetSize = FVector2D(600.0f, 400.0f);
	Projection.Window.ZoomFactor = 4.0f;
	Projection.Window.ViewCenterOffset = FVector2D(0.5f, 0.0f);
	const FVector PanContact = Projection.RadarOrigin +
		FRotator(0.0f, Projection.ReferenceRotation.Yaw, 0.0f).RotateVector(FVector(0.0f, 50000.0f, 0.0f));
	TestFalse(TEXT("PPI radar origin can leave the MFD window"),
		FRadarDisplayGeometryMath::Project(Projection.RadarOrigin, Projection, TrackPixel));
	TestTrue(TEXT("panned PPI shows a contact away from the origin"),
		FRadarDisplayGeometryMath::Project(PanContact, Projection, TrackPixel));
	TestTrue(TEXT("panned PPI inverse accepts symbol position"),
		FRadarDisplayGeometryMath::Unproject(TrackPixel, Projection, RecoveredWorld));
	TestTrue(TEXT("panned PPI cursor aligns with contact"),
		FVector::Dist2D(RecoveredWorld, PanContact) < 2.0f);
	TestFalse(TEXT("PPI rejects a coordinate outside the MFD window"),
		FRadarDisplayGeometryMath::Unproject(Projection.WidgetTopLeft + FVector2D(-1.0f, 200.0f),
			Projection, RecoveredWorld));
	FRadarDisplayWindow ExcessiveWindow;
	ExcessiveWindow.ZoomFactor = 4.0f;
	ExcessiveWindow.ViewCenterOffset = FVector2D(1.0f, -1.0f);
	const FRadarDisplayWindow Clamped = FRadarDisplayGeometryMath::ClampWindow(ExcessiveWindow);
	TestTrue(TEXT("one-kilometer map offset stays one kilometer"),
		Clamped.ViewCenterOffset.Equals(FVector2D(1.0f, -1.0f), 0.001f));
	Projection.Window = Clamped;
	Projection.Window.ZoomFactor = 1.0f;
	TestTrue(TEXT("one-kilometer PPI offset keeps the radar origin visible"),
		FRadarDisplayGeometryMath::Project(Projection.RadarOrigin, Projection, TrackPixel));
	return true;
}
#endif
