// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "RadarDisplayGeometry.h"

namespace
{
	constexpr float CentimetersPerKilometer = 100000.0f;

	bool IsProjectionValid(const FRadarDisplayProjection& P)
	{
		return (P.Geometry == ERadarDisplayGeometry::BScope || P.Geometry == ERadarDisplayGeometry::PPI) &&
			P.WidgetSize.X > KINDA_SMALL_NUMBER && P.WidgetSize.Y > KINDA_SMALL_NUMBER &&
			P.DisplayRangeCm > KINDA_SMALL_NUMBER &&
			P.AzimuthWidth > KINDA_SMALL_NUMBER && P.AzimuthWidth <= 360.0f &&
			FMath::IsFinite(P.WidgetTopLeft.X) && FMath::IsFinite(P.WidgetTopLeft.Y) &&
			FMath::IsFinite(P.WidgetSize.X) && FMath::IsFinite(P.WidgetSize.Y) &&
			FMath::IsFinite(P.AzimuthWidth) && FMath::IsFinite(P.ScanCenterAzimuth) &&
			FMath::IsFinite(P.CursorElevation) &&
			FMath::IsFinite(P.ReferenceRotation.Pitch) && FMath::IsFinite(P.ReferenceRotation.Yaw) &&
			FMath::IsFinite(P.ReferenceRotation.Roll) &&
			FMath::IsFinite(P.RadarOrigin.X) && FMath::IsFinite(P.RadarOrigin.Y) &&
			FMath::IsFinite(P.RadarOrigin.Z) && FMath::IsFinite(P.DisplayRangeCm) &&
			FMath::IsFinite(P.Window.ZoomFactor) && P.Window.ZoomFactor >= 1.0f &&
			FMath::IsFinite(P.Window.ViewCenterOffset.X) && FMath::IsFinite(P.Window.ViewCenterOffset.Y);
	}

	FVector2D BaseToWindow(const FVector2D& Base, const FRadarDisplayProjection& P)
	{
		return FVector2D(0.5f, 0.5f) +
			(Base - FVector2D(0.5f, 0.5f)) * P.Window.ZoomFactor;
	}

	FVector2D WindowToBase(const FVector2D& Window, const FRadarDisplayProjection& P)
	{
		return FVector2D(0.5f, 0.5f) +
			(Window - FVector2D(0.5f, 0.5f)) / P.Window.ZoomFactor;
	}

	bool InsideUnitSquare(const FVector2D& Point)
	{
		return Point.X >= 0.0f && Point.X <= 1.0f && Point.Y >= 0.0f && Point.Y <= 1.0f;
	}
}

FRadarDisplayWindow FRadarDisplayGeometryMath::ClampWindow(const FRadarDisplayWindow& Window)
{
	FRadarDisplayWindow Result;
	Result.ZoomFactor = FMath::IsFinite(Window.ZoomFactor) ? FMath::Clamp(Window.ZoomFactor, 1.0f, 16.0f) : 1.0f;
	Result.ViewCenterOffset.X = FMath::IsFinite(Window.ViewCenterOffset.X)
		? Window.ViewCenterOffset.X : 0.0f;
	Result.ViewCenterOffset.Y = FMath::IsFinite(Window.ViewCenterOffset.Y)
		? Window.ViewCenterOffset.Y : 0.0f;
	return Result;
}

bool FRadarDisplayGeometryMath::Project(const FVector& WorldLocation, const FRadarDisplayProjection& P, FVector2D& OutWidgetPosition)
{
	OutWidgetPosition = FVector2D::ZeroVector;
	if (!IsProjectionValid(P) || !FMath::IsFinite(WorldLocation.X) ||
		!FMath::IsFinite(WorldLocation.Y) || !FMath::IsFinite(WorldLocation.Z))
	{
		return false;
	}

	const FVector Delta = WorldLocation - P.RadarOrigin;
	if (P.Geometry == ERadarDisplayGeometry::BScope)
	{
		const FVector Local = P.ReferenceRotation.UnrotateVector(Delta);
		const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
		const float AzDelta = FMath::FindDeltaAngleDegrees(P.ScanCenterAzimuth, Bearing);
		const float SlantRange = Delta.Size();
		const FVector2D Base(0.5f + (AzDelta - P.Window.ViewCenterOffset.X) / P.AzimuthWidth,
			1.0f - (SlantRange - P.Window.ViewCenterOffset.Y * CentimetersPerKilometer) / P.DisplayRangeCm);
		const FVector2D Window = BaseToWindow(Base, P);
		OutWidgetPosition = P.WidgetTopLeft + Window * P.WidgetSize;
		return (P.AzimuthWidth >= 360.0f || FMath::Abs(AzDelta) <= P.AzimuthWidth * 0.5f) &&
			SlantRange <= P.DisplayRangeCm && InsideUnitSquare(Window);
	}

	const FRotator YawOnly(0.0f, P.bHeadingUp ? P.ReferenceRotation.Yaw : 0.0f, 0.0f);
	const FVector Local = YawOnly.UnrotateVector(FVector(Delta.X, Delta.Y, 0.0f));
	const float RadiusPixels = 0.5f * FMath::Min(P.WidgetSize.X, P.WidgetSize.Y);
	const FVector2D MapPoint(Local.Y, Local.X);
	const FVector2D WindowOffset((MapPoint.X - P.Window.ViewCenterOffset.X * CentimetersPerKilometer) / P.DisplayRangeCm,
		(P.Window.ViewCenterOffset.Y * CentimetersPerKilometer - MapPoint.Y) / P.DisplayRangeCm);
	OutWidgetPosition = P.WidgetTopLeft + 0.5f * P.WidgetSize +
		WindowOffset * (RadiusPixels * P.Window.ZoomFactor);
	const FVector2D Window = (OutWidgetPosition - P.WidgetTopLeft) / P.WidgetSize;
	return FVector2D(Delta.X, Delta.Y).SizeSquared() <= FMath::Square(P.DisplayRangeCm) && InsideUnitSquare(Window);
}

bool FRadarDisplayGeometryMath::Unproject(const FVector2D& WidgetPosition, const FRadarDisplayProjection& P, FVector& OutWorldLocation)
{
	OutWorldLocation = FVector::ZeroVector;
	if (!IsProjectionValid(P) || !FMath::IsFinite(WidgetPosition.X) || !FMath::IsFinite(WidgetPosition.Y))
	{
		return false;
	}

	if (P.Geometry == ERadarDisplayGeometry::BScope)
	{
		const FVector2D PixelOffset = WidgetPosition - P.WidgetTopLeft;
		const FVector2D Window(PixelOffset.X / P.WidgetSize.X, PixelOffset.Y / P.WidgetSize.Y);
		if (!InsideUnitSquare(Window))
		{
			return false;
		}
		const FVector2D Normalized = WindowToBase(Window, P);
		const float Bearing = P.ScanCenterAzimuth + (Normalized.X - 0.5f) * P.AzimuthWidth +
			P.Window.ViewCenterOffset.X;
		// Azimuth is undefined at exactly zero range. One centimeter retains a stable
		// direction while projecting to the bottom edge at normal widget resolutions.
		const float SlantRange = FMath::Max(1.0f,
			(1.0f - Normalized.Y) * P.DisplayRangeCm + P.Window.ViewCenterOffset.Y * CentimetersPerKilometer);
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(P.ScanCenterAzimuth, Bearing)) >
			P.AzimuthWidth * 0.5f + KINDA_SMALL_NUMBER ||
			SlantRange > P.DisplayRangeCm + KINDA_SMALL_NUMBER) return false;
		OutWorldLocation = P.RadarOrigin + P.ReferenceRotation.RotateVector(FRotator(P.CursorElevation, Bearing, 0.0f).Vector() * SlantRange);
		return true;
	}

	const float RadiusPixels = 0.5f * FMath::Min(P.WidgetSize.X, P.WidgetSize.Y);
	const FVector2D Window = (WidgetPosition - P.WidgetTopLeft) / P.WidgetSize;
	if (!InsideUnitSquare(Window)) return false;
	const FVector2D ScreenOffset = (WidgetPosition - P.WidgetTopLeft - 0.5f * P.WidgetSize) /
		(RadiusPixels * P.Window.ZoomFactor);
	const FVector2D Offset(ScreenOffset.X + P.Window.ViewCenterOffset.X * CentimetersPerKilometer / P.DisplayRangeCm,
		ScreenOffset.Y - P.Window.ViewCenterOffset.Y * CentimetersPerKilometer / P.DisplayRangeCm);
	if (Offset.SizeSquared() > 1.0f + KINDA_SMALL_NUMBER)
	{
		return false;
	}
	if (Offset.IsNearlyZero())
	{
		OutWorldLocation = P.RadarOrigin + P.ReferenceRotation.RotateVector(
			FRotator(P.CursorElevation, P.ScanCenterAzimuth, 0.0f).Vector());
		return true;
	}
	const FVector Local(-Offset.Y * P.DisplayRangeCm, Offset.X * P.DisplayRangeCm, 0.0f);
	const FRotator YawOnly(0.0f, P.bHeadingUp ? P.ReferenceRotation.Yaw : 0.0f, 0.0f);
	OutWorldLocation = P.RadarOrigin + YawOnly.RotateVector(Local);
	return true;
}

FVector FRadarDisplayGeometryMath::CursorToWorld(const FRadarCursorState& Cursor, const FRadarDisplayProjection& P)
{
	return P.RadarOrigin + P.ReferenceRotation.RotateVector(
		FRotator(Cursor.ElevationDegrees, Cursor.AzimuthDegrees, 0.0f).Vector() * Cursor.SlantRangeCm);
}
