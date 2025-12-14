// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "RadarDisplayGeometry.generated.h"

/** A B-scope plots local azimuth against slant range; a PPI plots horizontal position. */
UENUM(BlueprintType)
enum class ERadarDisplayGeometry : uint8
{
	BScope UMETA(DisplayName = "B-Scope (Azimuth / Slant Range)"),
	PPI UMETA(DisplayName = "PPI (Plan Position)")
};



USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FRadarDisplayView
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection")
	ERadarDisplayGeometry ActiveGeometry = ERadarDisplayGeometry::BScope;



	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection")
	bool bHeadingUp = true;
};

/** All distances are centimeters and all widget coordinates are local pixels, X right and Y down. */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FRadarDisplayProjection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection")
	ERadarDisplayGeometry Geometry = ERadarDisplayGeometry::BScope;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection")
	FVector RadarOrigin = FVector::ZeroVector;

	/** Stable platform orientation. A rotating antenna socket does not change this frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection")
	FRotator ReferenceRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection")
	FVector2D WidgetTopLeft = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection")
	FVector2D WidgetSize = FVector2D(512.0f, 512.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection", meta = (ClampMin = "1.0"))
	float DisplayRangeCm = 7408000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection")
	float ScanCenterAzimuth = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection", meta = (ClampMin = "1.0", ClampMax = "360.0"))
	float AzimuthWidth = 120.0f;

	/** Only used by the inverse B-scope projection, which has no elevation axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection")
	float CursorElevation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display Projection", meta = (EditCondition = "Geometry == ERadarDisplayGeometry::PPI"))
	bool bHeadingUp = true;

	/** Advanced API: use the radar's display view for ordinary widgets. */
};

/** Shared physical TDC position. It is independent of widget size and resolution. */
USTRUCT(BlueprintType)
struct WEAPONSYSTEMS_API FRadarCursorState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Display Projection|Target Cursor")
	float AzimuthDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Display Projection|Target Cursor")
	float ElevationDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Display Projection|Target Cursor")
	float SlantRangeCm = 0.0f;

	/** Current physical cursor location; use this with ProjectWorldToDisplay for the image. */
	UPROPERTY(BlueprintReadOnly, Category = "Display Projection|Target Cursor")
	FVector WorldLocation = FVector::ZeroVector;
};

/** Pure projection math, shared by the radar component and AircraftDisplay. */
struct WEAPONSYSTEMS_API FRadarDisplayGeometryMath
{
	static bool Project(const FVector& WorldLocation, const FRadarDisplayProjection& Projection, FVector2D& OutWidgetPosition);
	static bool Unproject(const FVector2D& WidgetPosition, const FRadarDisplayProjection& Projection, FVector& OutWorldLocation);
	static FVector CursorToWorld(const FRadarCursorState& Cursor, const FRadarDisplayProjection& Projection);
};
