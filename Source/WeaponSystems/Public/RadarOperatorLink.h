// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AircraftRadarComponent.h"
#include "RadarOperatorLink.generated.h"

/** One coherent radar display update for an authorized controller. */
USTRUCT()
struct FRadarOperatorSnapshot
{
	GENERATED_BODY()

	UPROPERTY() int32 Revision = 0;
	UPROPERTY() ERadarOperatingMode Mode = ERadarOperatingMode::Off;
	UPROPERTY() ERadarACMSubMode ACMMode = ERadarACMSubMode::Boresight;
	UPROPERTY() float DisplayRangeCm = 0.0f;
	UPROPERTY() FRadarDisplayView DisplayView;
	UPROPERTY() int32 DisplayViewRevision = 1;
	UPROPERTY() float ScanAzimuth = 0.0f;
	UPROPERTY() float ScanCenterAzimuth = 0.0f;
	UPROPERTY() float ScanCenterElevation = 0.0f;
	UPROPERTY() float AzimuthScanWidth = 120.0f;
	UPROPERTY() float ElevationScanHeight = 20.0f;
	UPROPERTY() int32 ElevationBars = 4;
	UPROPERTY() ERadarScanDrive ScanDrive = ERadarScanDrive::VirtualMechanical;
	UPROPERTY() ERadarScanSize ScanSizePreset = ERadarScanSize::Custom;
	UPROPERTY() int32 LockedTrackID = -1;
	UPROPERTY() int32 BuggedTrackID = -1;
	UPROPERTY() int32 ScanBar = 0;
	UPROPERTY() uint32 ScanProgressRevision = 0;
	UPROPERTY() float ScanBarLevel = 0.0f;
	UPROPERTY() float ScanSweepLevel = 0.0f;
	UPROPERTY() bool bScanningRight = true;
	UPROPERTY() int32 SweepCounter = 0;
	UPROPERTY() int32 SARImageRevision = 0;
	UPROPERTY() FRadarCursorState Cursor;
	UPROPERTY() bool bTargetCursorEnabled = true;
	UPROPERTY() TArray<FRadarTrack> Tracks;
	UPROPERTY() TArray<FRadarTrack> DisplayTracks;
	UPROPERTY() TArray<FRadarLaunchedMissileStatus> LaunchedRadarMissiles;
	UPROPERTY() int32 SelectedLinkedTrackID = -1;
	UPROPERTY() int32 SelectedContactID = 0;
	UPROPERTY() bool bDataLinkConnected = false;
	UPROPERTY() FName DataLinkNetworkID = NAME_None;
	UPROPERTY() int32 DataLinkParticipantID = 0;
	UPROPERTY() FVector SpotlightPoint = FVector::ZeroVector;
	UPROPERTY() bool bHasSpotlightPoint = false;
	UPROPERTY() float RadarAltitudeCm = 0.0f;
	UPROPERTY() bool bRadarAltitudeValid = false;
};

/** Owner-only command and display channel for a local cockpit/station operator. */
UCLASS(NotBlueprintable)
class WEAPONSYSTEMS_API ARadarOperatorLink : public AActor
{
	GENERATED_BODY()

public:
	ARadarOperatorLink();

	void Initialize(UAircraftRadarComponent* InRadar);
	void PushSnapshot(const FRadarOperatorSnapshot& NewSnapshot);
	void PushEvent(const FRadarOperatorEvent& Event, int32 MinimumSnapshotRevision);
	int32 SubmitCommand(ERadarCommandType Command, int32 IntValue = 0, float ValueA = 0.0f,
		float ValueB = 0.0f, const FVector& WorldValue = FVector::ZeroVector,
		const FRadarCursorState& Cursor = FRadarCursorState(),
		ERadarDisplayGeometry Geometry = ERadarDisplayGeometry::BScope, int32 ViewRevision = -1);
	void SubmitCursorInput(float X, float Y);
	FVector2D GetActiveCursorInput(float WorldTime) const;

	UAircraftRadarComponent* GetRadar() const { return Radar; }
	UFUNCTION(BlueprintPure, Category = "Radar|Multiplayer")
	bool CanControl() const { return bCanControl; }
	void SetCanControl(bool bInCanControl);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION() void OnRep_Radar();
	UFUNCTION() void OnRep_Snapshot();

	UFUNCTION(Server, Reliable)
	void ServerSubmitCommand(int32 RequestID, ERadarCommandType Command, int32 IntValue,
		float ValueA, float ValueB, FVector WorldValue, FRadarCursorState Cursor,
		ERadarDisplayGeometry Geometry, int32 ViewRevision);

	UFUNCTION(Server, Unreliable)
	void ServerSubmitCursorInput(float X, float Y, int32 ViewRevision);

	UFUNCTION(Client, Reliable)
	void ClientCommandResult(int32 RequestID, ERadarCommandType Command, bool bSuccess, int32 TrackID);

	UFUNCTION(Client, Reliable)
	void ClientRadarEvent(FRadarOperatorEvent Event, int32 MinimumSnapshotRevision);

private:
	UPROPERTY(ReplicatedUsing = OnRep_Radar)
	TObjectPtr<UAircraftRadarComponent> Radar = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FRadarOperatorSnapshot Snapshot;

	UPROPERTY(Replicated)
	bool bCanControl = false;

	int32 NextRequestID = 1;
	FVector2D CursorInput = FVector2D::ZeroVector;
	float LastCursorInputTime = -1000.0f;
	int32 LastCursorInputViewRevision = -1;
	FVector2D LastSentCursorInput = FVector2D::ZeroVector;
	float LastSentCursorInputTime = -1000.0f;
	int32 LastSentCursorInputViewRevision = -1;
	int32 AppliedRevision = -1;
	struct FPendingEvent
	{
		FRadarOperatorEvent Event;
		int32 MinimumSnapshotRevision = 0;
	};
	TArray<FPendingEvent> PendingEvents;
	void FlushPendingEvents();
	struct FPendingResult
	{
		int32 RequestID = INDEX_NONE;
		ERadarCommandType Command = ERadarCommandType::SetMode;
		bool bSuccess = false;
		int32 TrackID = -1;
	};
	TArray<FPendingResult> PendingResults;
	void FlushPendingResults();
};
